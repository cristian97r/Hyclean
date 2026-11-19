// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef EGLANTINE_DLSS

#include "common/httplib.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>
#include <fmt/format.h>
#ifdef _WIN32
#include <zlib.h>

#include "video_core/vulkan_common/streamline.h"
#else
#include <openssl/sha.h>

#include "video_core/dlss_module_release.h"
#include "video_core/renderer_vulkan/vk_ngx_dlss.h"
#endif
#include "common/common_types.h"
#include "common/fs/fs_util.h"
#include "common/logging.h"
#include "lichii/configuration/nvidia_dlss_panel.h"

#include "common/net/ca_store.h"

namespace {

QString Tr(const char* text) {
    return QCoreApplication::translate("NvidiaDlssPanel", text);
}

#ifdef _WIN32

namespace Streamline = Vulkan::Streamline;

constexpr const char* RELEASE_PAGE = "https://github.com/NVIDIA-RTX/Streamline/releases";
constexpr const char* RELEASE_VERSION = "2.14.16";
constexpr const char* ZIP_HOST = "https://github.com";
constexpr const char* ZIP_PATH =
    "/NVIDIA-RTX/Streamline/releases/download/v2.14.16/streamline-sdk-v2.14.16.zip";
constexpr const char* INTERPOSER_ENTRY = "bin/x64/sl.interposer.dll";
constexpr const char* MODEL_ENTRY = "bin/x64/nvngx_dlss.dll";
constexpr const char* STREAMLINE_LICENSE_ENTRY = "license.txt";
constexpr const char* DLSS_LICENSE_ENTRY = "bin/x64/nvngx_dlss.license.txt";
constexpr const char* REFLEX_LICENSE_ENTRY = "bin/x64/reflex.license.txt";
constexpr const char* BIN_DIR = "bin/x64/";

std::string FileName(const std::string& entry) {
    return Common::FS::PathToUTF8String(std::filesystem::path{entry}.filename());
}

u16 U16(const std::string& data, size_t at) {
    return static_cast<u16>(static_cast<u8>(data[at]) | static_cast<u8>(data[at + 1]) << 8);
}

u32 U32(const std::string& data, size_t at) {
    return static_cast<u32>(U16(data, at)) | static_cast<u32>(U16(data, at + 2)) << 16;
}

u64 U64(const std::string& data, size_t at) {
    return static_cast<u64>(U32(data, at)) | static_cast<u64>(U32(data, at + 4)) << 32;
}

struct ZipEntry {
    u16 method{};
    u32 crc{};
    u64 compressed{};
    u64 size{};
    u64 offset{};
};

/// Reads single files out of a zip on a web server with range requests, so only the files
/// needed are downloaded (about 32 MB of the 276 MB release).
class RemoteZip {
public:
    /// Follows GitHub's redirect to the file's (short-lived) download address.
    bool Resolve() {
        host = ZIP_HOST;
        path = ZIP_PATH;
        for (int redirect = 0; redirect < 5; ++redirect) {
            const auto result = MakeClient()->Get(path, httplib::Headers{{"Range", "bytes=0-0"}});
            if (!result) {
                error = httplib::to_string(result.error());
                return false;
            }
            if (result->status == 206) {
                // "bytes 0-0/<size>"; the server doesn't take "last N bytes" ranges.
                const std::string range = result->get_header_value("Content-Range");
                const size_t slash = range.rfind('/');
                zip_size = slash == std::string::npos
                               ? 0
                               : std::strtoull(range.c_str() + slash + 1, nullptr, 10);
                if (zip_size == 0) {
                    error = "no file size";
                    return false;
                }
                return true;
            }
            const std::string location = result->get_header_value("Location");
            const size_t scheme = location.find("://");
            const size_t slash =
                scheme == std::string::npos ? std::string::npos : location.find('/', scheme + 3);
            if (result->status < 300 || result->status >= 400 || slash == std::string::npos) {
                error = fmt::format("HTTP {}", result->status);
                return false;
            }
            host = location.substr(0, slash);
            path = location.substr(slash);
        }
        error = "too many redirects";
        return false;
    }

    bool Open() {
        if (!Resolve()) {
            return false;
        }
        const u64 tail_start = zip_size > 65558 ? zip_size - 65558 : 0;
        const std::optional<std::string> tail =
            Fetch(fmt::format("bytes={}-{}", tail_start, zip_size - 1));
        if (!tail) {
            return false;
        }
        const size_t end = tail->rfind(std::string{"PK\x05\x06", 4});
        if (end == std::string::npos || end + 22 > tail->size()) {
            error = "no zip directory";
            return false;
        }
        u64 directory_size = U32(*tail, end + 12);
        u64 directory_offset = U32(*tail, end + 16);
        if (directory_offset == 0xFFFFFFFF && end >= 20 &&
            tail->compare(end - 20, 4, std::string{"PK\x06\x07", 4}) == 0) {
            const u64 record = U64(*tail, end - 20 + 8);
            const std::optional<std::string> zip64 =
                Fetch(fmt::format("bytes={}-{}", record, record + 55));
            if (!zip64 || zip64->size() < 56) {
                return false;
            }
            directory_size = U64(*zip64, 40);
            directory_offset = U64(*zip64, 48);
        }
        const std::optional<std::string> directory = Fetch(
            fmt::format("bytes={}-{}", directory_offset, directory_offset + directory_size - 1));
        if (!directory) {
            return false;
        }
        for (size_t at = 0; at + 46 <= directory->size() && U32(*directory, at) == 0x02014b50;) {
            const u16 name_length = U16(*directory, at + 28);
            const u16 extra_length = U16(*directory, at + 30);
            const u16 comment_length = U16(*directory, at + 32);
            ZipEntry entry{
                .method = U16(*directory, at + 10),
                .crc = U32(*directory, at + 16),
                .compressed = U32(*directory, at + 20),
                .size = U32(*directory, at + 24),
                .offset = U32(*directory, at + 42),
            };
            const size_t extra_end = at + 46 + name_length + extra_length;
            if (extra_end + comment_length > directory->size()) {
                error = "bad zip directory";
                return false;
            }
            const std::string name = directory->substr(at + 46, name_length);
            // Zip64 sizes and offset, in this order, for the fields that overflowed.
            for (size_t extra = at + 46 + name_length; extra + 4 <= extra_end;) {
                const u16 id = U16(*directory, extra);
                const u16 length = U16(*directory, extra + 2);
                const size_t record_end = std::min<size_t>(extra + 4 + length, extra_end);
                if (id == 0x0001) {
                    size_t field = extra + 4;
                    for (u64* value : {&entry.size, &entry.compressed, &entry.offset}) {
                        if (*value == 0xFFFFFFFF && field + 8 <= record_end) {
                            *value = U64(*directory, field);
                            field += 8;
                        }
                    }
                }
                extra += 4 + length;
            }
            entries.emplace(name, entry);
            at += 46 + name_length + extra_length + comment_length;
        }
        return true;
    }

    const ZipEntry* Find(const std::string& name) const {
        const auto it = entries.find(name);
        return it == entries.end() ? nullptr : &it->second;
    }

    std::optional<std::string> Read(const ZipEntry& entry,
                                    const std::function<bool(u64)>& progress = {}) {
        const std::optional<std::string> header =
            Fetch(fmt::format("bytes={}-{}", entry.offset, entry.offset + 29));
        if (!header) {
            return std::nullopt;
        }
        if (header->size() < 30 || U32(*header, 0) != 0x04034b50) {
            error = "bad zip entry";
            return std::nullopt;
        }
        const u64 start = entry.offset + 30 + U16(*header, 26) + U16(*header, 28);
        std::string data;
        if (entry.compressed != 0) {
            std::optional<std::string> fetched =
                Fetch(fmt::format("bytes={}-{}", start, start + entry.compressed - 1), progress);
            if (!fetched) {
                return std::nullopt;
            }
            data = std::move(*fetched);
        }
        std::string out;
        if (entry.method == 0) {
            out = std::move(data);
        } else if (entry.method == 8) {
            out.resize(entry.size);
            z_stream stream{};
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
                error = "zlib";
                return std::nullopt;
            }
            stream.next_in = reinterpret_cast<Bytef*>(data.data());
            stream.avail_in = static_cast<uInt>(data.size());
            stream.next_out = reinterpret_cast<Bytef*>(out.data());
            stream.avail_out = static_cast<uInt>(out.size());
            const int result = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (result != Z_STREAM_END || stream.total_out != entry.size) {
                error = "could not unpack";
                return std::nullopt;
            }
        } else {
            error = "unknown zip compression";
            return std::nullopt;
        }
        const uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(out.data()),
                                static_cast<uInt>(out.size()));
        if (crc != entry.crc) {
            error = "damaged download (checksum)";
            return std::nullopt;
        }
        return out;
    }

    bool canceled{};
    std::string error;

private:
    std::unique_ptr<httplib::Client> MakeClient() const {
        auto client = std::make_unique<httplib::Client>(host);
        client->set_connection_timeout(30);
        client->set_read_timeout(30);
        client->set_write_timeout(30);
        Common::Net::ConfigureCaStore(*client);
        return client;
    }

    std::optional<std::string> Fetch(const std::string& range,
                                     const std::function<bool(u64)>& progress = {}) {
        std::string body;
        int status = 0;
        const auto result = MakeClient()->Get(
            path, httplib::Headers{{"Range", range}},
            [&status](const httplib::Response& response) {
                status = response.status;
                return response.status == 206;
            },
            [&](const char* data, size_t length) {
                body.append(data, length);
                if (progress && !progress(body.size())) {
                    canceled = true;
                    return false;
                }
                return true;
            });
        if (!result || status != 206) {
            error = result ? fmt::format("HTTP {}", status) : httplib::to_string(result.error());
            return std::nullopt;
        }
        return body;
    }

    std::string host;
    std::string path;
    u64 zip_size{};
    std::map<std::string, ZipEntry> entries;
};

QString Named(const QString& name, const std::string& version) {
    return version.empty() ? name : QStringLiteral("%1 %2").arg(name, QString::fromStdString(version));
}

QString PathText(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}
#else
// Every file has its SHA-256 checked: the module (Lichii's MIT code) against
// the .sha256 of its latest release, NVIDIA's DLSS model runtime and licence
// against sums pinned here, from NVIDIA's DLSS repository at a pinned commit.
constexpr const char* NGX_COMMIT = "9981c6350dc58c0ce21f024fd65e31e851c31689";
constexpr const char* RUNTIME_NAME = "libnvidia-ngx-dlss.so.310.9.16";
constexpr const char* RUNTIME_SHA256 =
    "30982c937afed85c96b8d313323de9a650a2eec6de1bb0f714b21750f4ce98f6";
constexpr const char* LICENSE_SHA256 =
    "d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0";
constexpr const char* MODULE_NAME = "lichii-dlss-module.so";
constexpr const char* MODULE_RELEASE_PAGE = "https://github.com/tasarren/lichii-dlss/releases";
constexpr const char* MODULE_LATEST_RELEASE =
    "https://api.github.com/repos/tasarren/lichii-dlss/releases/latest";

struct Downloadable {
    std::string url;
    std::string sha256;
    std::filesystem::path target;
};

std::vector<Downloadable> WantedFiles(const std::filesystem::path& folder,
                                      const std::string& module_url,
                                      const std::string& module_sha256) {
    return {
        {module_url, module_sha256, folder / MODULE_NAME},
        {fmt::format("https://raw.githubusercontent.com/NVIDIA/DLSS/{}/lib/Linux_x86_64/rel/{}",
                     NGX_COMMIT, RUNTIME_NAME),
         RUNTIME_SHA256, folder / RUNTIME_NAME},
        {fmt::format("https://raw.githubusercontent.com/NVIDIA/DLSS/{}/LICENSE.txt", NGX_COMMIT),
         LICENSE_SHA256, folder / "NVIDIA-LICENSE.txt"},
    };
}

bool Sha256Matches(const std::string& data, std::string_view expected) {
    std::array<u8, SHA256_DIGEST_LENGTH> digest;
    SHA256(reinterpret_cast<const u8*>(data.data()), data.size(), digest.data());
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const u8 byte : digest) {
        hex += "0123456789abcdef"[byte >> 4];
        hex += "0123456789abcdef"[byte & 0xf];
    }
    return hex == expected;
}

/// One GET saved whole into data, with the SHA-256 checked by the caller.
/// Sets status to the HTTP status, or 0 when no reply came.
bool FetchFile(const std::string& url, const std::function<bool(u64)>& progress,
               std::string& error, std::string& data, int* status = nullptr) {
    const size_t scheme = url.find("://");
    const size_t host_end = scheme == std::string::npos ? 0 : url.find('/', scheme + 3);
    if (scheme == std::string::npos || host_end == std::string::npos) {
        error = "bad download address";
        return false;
    }
    httplib::Client client(url.substr(0, host_end));
    client.set_connection_timeout(30);
    client.set_read_timeout(30);
    client.set_follow_location(true);
    Common::Net::ConfigureCaStore(client);
    data.clear();
    // GitHub's API refuses requests without a User-Agent.
    const httplib::Headers headers{{"User-Agent", "Lichii"}};
    const auto result = client.Get(url.substr(host_end), headers,
                                   [&](const char* part, size_t length) {
                                       data.append(part, length);
                                       return !progress || progress(data.size());
                                   });
    if (status) {
        *status = result ? result->status : 0;
    }
    if (!result || result->status != 200) {
        error = result ? fmt::format("HTTP {}", result->status)
                       : httplib::to_string(result.error());
        return false;
    }
    return true;
}

/// The module's address and sum from its latest release, or the reason for the panel.
std::optional<std::pair<std::string, std::string>> LatestModule(std::string& error) {
    std::string reply;
    int status = 0;
    if (!FetchFile(MODULE_LATEST_RELEASE, {}, error, reply, &status)) {
        // A private or empty repository answers 404 too.
        if (status == 404) {
            error = "Lichii's DLSS module release is not available yet";
        } else if (status == 403 || status == 429) {
            error = "GitHub's download limit is reached; try again in an hour";
        } else if (status == 0) {
            error = "no network connection (" + error + ")";
        }
        return std::nullopt;
    }
    const auto release = VideoCommon::PickDlssModuleRelease(reply);
    if (!release) {
        error = "Lichii's DLSS module release is not available yet";
        return std::nullopt;
    }
    std::string sum_text;
    if (!FetchFile(release->sha256_url, {}, error, sum_text)) {
        return std::nullopt;
    }
    const auto sum = VideoCommon::ParseSha256Line(sum_text);
    if (!sum) {
        error = "the module release has a broken .sha256 file";
        return std::nullopt;
    }
    LOG_INFO(Frontend, "Lichii DLSS module release {}", release->tag);
    return std::pair{release->module_url, *sum};
}

std::filesystem::path Folder() {
    return std::filesystem::path{Vulkan::Ngx::ModuleDir()};
}
#endif

} // Anonymous namespace

NvidiaDlssPanel::NvidiaDlssPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 0, 0, 8);
    layout->setSpacing(6);

    QHBoxLayout* status_row = new QHBoxLayout;
    status_row->setSpacing(8);
    icon_label = new QLabel(this);
    icon_label->setTextFormat(Qt::RichText);
    icon_label->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    icon_label->setFixedWidth(16);
    status_label = new QLabel(this);
    status_label->setTextFormat(Qt::RichText);
    status_label->setWordWrap(true);
    status_row->addWidget(icon_label);
    status_row->addWidget(status_label, 1);
#ifndef _WIN32
    reinstall_link =
        new QLabel(QStringLiteral("<a href=\"#\">%1</a>").arg(Tr("Reinstall")), this);
    reinstall_link->setAlignment(Qt::AlignTop);
    reinstall_link->setToolTip(Tr("Show Download and Install from file again."));
    status_row->addWidget(reinstall_link);
#endif

    QHBoxLayout* button_row = new QHBoxLayout;
    button_row->setContentsMargins(24, 0, 0, 0);
    download_button = new QPushButton(this);
#ifdef _WIN32
    folder_button = new QPushButton(Tr("Open Folder"), this);
#else
    folder_button = new QPushButton(Tr("Install from file..."), this);
    folder_button->setToolTip(Tr("Pick a lichii-dlss-module.so you downloaded. NVIDIA's DLSS model "
                                 "(libnvidia-ngx-dlss.so.*) has to be in the same folder."));
#endif
    QLabel* about = new QLabel(
        QStringLiteral("<a href=\"%1\">%2</a>").arg(QString::fromLatin1(
#ifdef _WIN32
                                                         RELEASE_PAGE),
#else
                                                         MODULE_RELEASE_PAGE),
#endif
                                                   Tr("Where do these files come from?")),
        this);
    about->setOpenExternalLinks(true);
#ifdef _WIN32
    about->setToolTip(Tr("NVIDIA's Streamline releases on GitHub. The files are in bin\\x64 of "
                         "the release zip."));
#else
    about->setToolTip(Tr("Lichii's DLSS module release on GitHub, and NVIDIA's DLSS model from "
                         "NVIDIA's DLSS repository at a pinned commit."));
#endif
    button_row->addWidget(download_button);
    button_row->addWidget(folder_button);
    button_row->addStretch(1);
#ifndef _WIN32
    QLabel* open_folder =
        new QLabel(QStringLiteral("<a href=\"#\">%1</a>").arg(Tr("Open folder")), this);
    button_row->addWidget(open_folder);
#endif
    button_row->addWidget(about);

    layout->addLayout(status_row);
    layout->addLayout(button_row);

    connect(download_button, &QPushButton::clicked, this, [this] { Download(); });
#ifdef _WIN32
    connect(folder_button, &QPushButton::clicked, this, [] {
        const std::filesystem::path folder = Streamline::FindDlssFiles().folder;
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(PathText(folder)));
    });
#else
    connect(folder_button, &QPushButton::clicked, this, [this] { InstallFromFile(); });
    connect(reinstall_link, &QLabel::linkActivated, this, [this] {
        reinstall_shown = true;
        Refresh();
    });
    connect(open_folder, &QLabel::linkActivated, this, [] {
        const std::filesystem::path folder = Folder();
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(folder.string())));
    });
#endif
    Refresh();
}

void NvidiaDlssPanel::showEvent(QShowEvent* event) {
    Refresh();
    QWidget::showEvent(event);
}

void NvidiaDlssPanel::Refresh() {
#ifdef _WIN32
    const Streamline::DlssFiles files = Streamline::FindDlssFiles();
    QStringList streamline_missing;
    if (files.interposer.empty()) {
        streamline_missing << QStringLiteral("sl.interposer.dll");
    }
    for (const std::string& name : files.dlss_missing) {
        streamline_missing << QString::fromStdString(name);
    }
    const bool has_streamline = streamline_missing.isEmpty() && !files.dlss_mixed;
    const QString streamline =
        !streamline_missing.isEmpty()
            ? Tr("NVIDIA Streamline (%1)").arg(streamline_missing.join(QStringLiteral(", ")))
            : Tr("NVIDIA Streamline's files (sl.*) from one release; these are from different "
                 "ones");
    const bool has_model = !files.model.empty() || !files.app_model.empty();
    const QString dlss = !files.model.empty()
                             ? Named(QStringLiteral("DLSS"), files.model_version)
                             : Tr("%1 from the NVIDIA app")
                                   .arg(Named(QStringLiteral("DLSS"), files.app_model_version));

    const bool has_frame_gen =
        !frame_gen_wanted || (files.frame_gen_missing.empty() && !files.streamline_mixed);
    QStringList frame_gen_missing;
    for (const std::string& name : files.frame_gen_missing) {
        frame_gen_missing << QString::fromStdString(name);
    }
    QString frame_gen;
    if (frame_gen_wanted) {
        frame_gen = !frame_gen_missing.isEmpty()
                        ? Tr("DLSS Frame Generation also needs %1.")
                              .arg(frame_gen_missing.join(QStringLiteral(", ")))
                    : files.streamline_mixed
                        ? Tr("DLSS Frame Generation needs NVIDIA's Streamline files (sl.*) from "
                             "one release; these are from different ones.")
                        : Tr("DLSS Frame Generation's files are present.");
    }

    QString status;
    QString button;
    if (has_streamline && has_model) {
        status =
            Tr("<b>Ready.</b> Found %1 and %2.<br>DLSS and the settings below apply only "
               "when a game uses its own DLSS. While DLSS runs, the status bar shows DLSS, "
               "its mode and its preset.")
                .arg(Named(QStringLiteral("NVIDIA Streamline"), files.interposer_version), dlss);
        button = Tr("Download Frame Generation files from NVIDIA");
    } else if (has_model) {
        status = Tr("<b>Files needed:</b> %1. %2 will be used. Until then, games use AMD FSR "
                    "3.1.")
                     .arg(streamline, dlss);
        button = has_frame_gen ? Tr("Download from NVIDIA (1 MB)") : Tr("Download from NVIDIA");
    } else if (has_streamline) {
        status = Tr("<b>One file needed:</b> NVIDIA DLSS (nvngx_dlss.dll). Until then, games use "
                    "AMD FSR 3.1.");
        button = has_frame_gen ? Tr("Download from NVIDIA (31 MB)") : Tr("Download from NVIDIA");
    } else {
        status = Tr("<b>NVIDIA's files needed:</b> %1 and DLSS (nvngx_dlss.dll). Until then, "
                    "games use AMD FSR 3.1.")
                     .arg(streamline);
        button = has_frame_gen ? Tr("Download from NVIDIA (32 MB)") : Tr("Download from NVIDIA");
    }
    if (!frame_gen.isEmpty()) {
        status += QStringLiteral(" ") + frame_gen;
    }
    const bool ready = has_streamline && has_model;
    icon_label->setText(ready ? QStringLiteral("<span style=\"color:#2ea043\">&#10004;</span>")
                              : QStringLiteral("<span style=\"color:#d29922\">&#9888;</span>"));
    status_label->setText(status);
    status_label->setToolTip(Tr("Folder: %1").arg(PathText(files.folder)));
    download_button->setText(button);
    download_button->setVisible(!ready || !has_frame_gen);
#else
    const Vulkan::Ngx::State state = Vulkan::Ngx::Probe();
    QString status;
    bool ready = false;
    switch (state.setup) {
    case Vulkan::Ngx::Setup::Ready:
        ready = true;
        status = Tr("<b>Ready.</b> Found Lichii's DLSS module (version %1) and the DLSS model "
                    "(%2).<br>While DLSS runs, the status bar shows DLSS, its mode and its "
                    "preset.")
                     .arg(QString::fromStdString(state.module_version),
                          QString::fromStdString(state.dlss_version));
        break;
    case Vulkan::Ngx::Setup::NotInstalled:
        status = Tr("<b>DLSS is not set up.</b> Press Download: Lichii fetches Lichii's DLSS "
                    "module and NVIDIA's DLSS files (about 57 MB). NVIDIA's licence is shown "
                    "first. Until then, games use AMD FSR 3.1.");
        break;
    case Vulkan::Ngx::Setup::WrongAbi:
        status = Tr("<b>Lichii's DLSS module is from another Lichii version.</b> Press Download "
                    "to set it up again. Until then, games use AMD FSR 3.1.");
        break;
    case Vulkan::Ngx::Setup::NoNvidia:
        status = Tr("<b>DLSS needs an NVIDIA GPU with NGX in the driver "
                    "(libnvidia-ngx.so.1).</b> It was not found, so games use AMD FSR 3.1.");
        break;
    case Vulkan::Ngx::Setup::InitFailed:
        status = Tr("<b>DLSS could not start: %1.</b> The log has the details. Until then, games "
                    "use AMD FSR 3.1.")
                     .arg(QString::fromStdString(state.detail));
        break;
    }
    icon_label->setText(ready ? QStringLiteral("<span style=\"color:#2ea043\">&#10004;</span>")
                              : QStringLiteral("<span style=\"color:#d29922\">&#9888;</span>"));
    status_label->setText(status);
    status_label->setToolTip(
        Tr("Folder: %1").arg(QString::fromStdString(Vulkan::Ngx::ModuleDir())));
    download_button->setText(Tr("Download"));
    const bool show_install = !ready || reinstall_shown;
    download_button->setVisible(show_install);
    folder_button->setVisible(show_install);
    reinstall_link->setVisible(!show_install);
#endif
}

void NvidiaDlssPanel::SetFrameGenWanted(bool wanted) {
    if (frame_gen_wanted == wanted) {
        return;
    }
    frame_gen_wanted = wanted;
    Refresh();
}

void NvidiaDlssPanel::Download() {
#ifdef _WIN32
    const Streamline::DlssFiles files = Streamline::FindDlssFiles();
    std::vector<std::string> wanted;
    // DLSS's Streamline plugins and frame generation's files each come as one set with the loader,
    // all from this release: Streamline loads a module only with the sl.common.dll and loader of
    // its own release.
    if (files.interposer.empty() || !files.dlss_missing.empty() || files.dlss_mixed) {
        wanted.emplace_back(INTERPOSER_ENTRY);
        for (const char* const name : Streamline::DlssPluginFiles()) {
            wanted.push_back(BIN_DIR + std::string{name});
        }
    }
    if (files.model.empty() && files.app_model.empty()) {
        wanted.emplace_back(MODEL_ENTRY);
    }
    std::vector<std::string> frame_gen_set;
    if (frame_gen_wanted && (!files.frame_gen_missing.empty() || files.streamline_mixed)) {
        const auto add = [&wanted, &frame_gen_set](std::string entry) {
            if (std::ranges::find(wanted, entry) == wanted.end()) {
                frame_gen_set.push_back(std::move(entry));
            }
        };
        add(INTERPOSER_ENTRY);
        for (const char* const name : Streamline::FrameGenFiles()) {
            add(BIN_DIR + std::string{name});
        }
    }
    const auto in_frame_gen_set = [&frame_gen_set](const std::string& entry) {
        return std::ranges::find(frame_gen_set, entry) != frame_gen_set.end();
    };
    wanted.insert(wanted.end(), frame_gen_set.begin(), frame_gen_set.end());
    if (wanted.empty()) {
        return;
    }
    // Where Streamline loads from: the loader's folder, or the user folder when there is none yet.
    const std::filesystem::path folder = files.folder;
    const auto fail = [this](const std::string& error) {
        LOG_ERROR(Frontend, "NVIDIA DLSS download failed: {}", error);
        QMessageBox::warning(this, Tr("NVIDIA DLSS"),
                             Tr("Could not download NVIDIA's files: %1")
                                 .arg(QString::fromStdString(error)));
        Refresh();
    };

    RemoteZip zip;
    std::string licences;
    {
        QProgressDialog busy(Tr("Contacting NVIDIA's download..."), QString{}, 0, 0, this);
        busy.setWindowTitle(Tr("NVIDIA DLSS"));
        busy.setWindowModality(Qt::WindowModal);
        busy.setMinimumDuration(0);
        busy.show();
        QApplication::processEvents();
        if (!zip.Open()) {
            busy.close();
            fail(zip.error);
            return;
        }
        const auto wants = [&wanted](const std::string& name) {
            return std::ranges::find(wanted, BIN_DIR + name) != wanted.end();
        };
        std::vector<std::pair<QString, const char*>> texts{
            {Tr("NVIDIA Streamline"), STREAMLINE_LICENSE_ENTRY}};
        if (std::ranges::find(wanted, MODEL_ENTRY) != wanted.end() || wants("nvngx_dlssg.dll")) {
            texts.emplace_back(Tr("NVIDIA DLSS"), DLSS_LICENSE_ENTRY);
        }
        if (wants("sl.reflex.dll") || wants("NvLowLatencyVk.dll")) {
            texts.emplace_back(Tr("NVIDIA Reflex"), REFLEX_LICENSE_ENTRY);
        }
        for (const auto& [title, entry_name] : texts) {
            const ZipEntry* const entry = zip.Find(entry_name);
            const std::optional<std::string> text = entry ? zip.Read(*entry) : std::nullopt;
            if ((!text || text->empty()) && entry_name == REFLEX_LICENSE_ENTRY) {
                LOG_WARNING(Frontend, "NVIDIA's release has no {}", entry_name);
                continue;
            }
            if (!text || text->empty()) {
                busy.close();
                fail("could not retrieve NVIDIA's licence text");
                return;
            }
            licences += fmt::format("==== {} ====\n\n{}\n\n", title.toStdString(),
                                    *text);
        }
    }

    // Without every file of the frame generation set in the release, none of it is taken.
    if (const auto missing = std::ranges::find_if(
            frame_gen_set, [&zip](const std::string& name) { return !zip.Find(name); });
        missing != frame_gen_set.end()) {
        LOG_WARNING(Frontend,
                    "NVIDIA's Streamline {} release has no {}; skipping DLSS Frame Generation's "
                    "files",
                    RELEASE_VERSION, *missing);
        std::erase_if(wanted, in_frame_gen_set);
        frame_gen_set.clear();
    }
    if (wanted.empty()) {
        fail("the release has none of the missing files");
        return;
    }
    QStringList names;
    for (const std::string& entry : wanted) {
        names << QString::fromStdString(FileName(entry));
    }
    QDialog dialog(this);
    dialog.setWindowTitle(Tr("Download NVIDIA DLSS"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* intro = new QLabel(
        Tr("Lichii will download %1 from NVIDIA's Streamline %2 release on GitHub into %3.<br>"
           "These files are NVIDIA's, under NVIDIA's licence terms below.")
            .arg(names.join(QStringLiteral(", ")), QString::fromLatin1(RELEASE_VERSION),
                 PathText(folder)),
        &dialog);
    intro->setWordWrap(true);
    QPlainTextEdit* licence_text = new QPlainTextEdit(QString::fromStdString(licences), &dialog);
    licence_text->setReadOnly(true);
    licence_text->setMinimumSize(640, 380);
    QDialogButtonBox* buttons = new QDialogButtonBox(&dialog);
    buttons->addButton(Tr("Accept and Download"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(intro);
    layout->addWidget(licence_text, 1);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    u64 total = 0;
    std::vector<const ZipEntry*> entries;
    for (const std::string& name : wanted) {
        const ZipEntry* const entry = zip.Find(name);
        if (!entry) {
            fail(name + " is not in the release");
            return;
        }
        entries.push_back(entry);
        total += entry->compressed;
    }
    QProgressDialog progress(Tr("Downloading NVIDIA's DLSS files..."), Tr("Cancel"), 0, 1000,
                             this);
    progress.setWindowTitle(Tr("NVIDIA DLSS"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    QApplication::processEvents();
    // The download address from the first step only lasts a few minutes.
    if (!zip.Resolve()) {
        progress.close();
        fail(zip.error);
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    u64 done = 0;
    // Every file is checked before any is replaced; frame generation's set is saved whole or not
    // at all, so the loader's folder never mixes releases.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> verified;
    const auto discard = [&verified, &ec] {
        for (const auto& [partial, target] : verified) {
            std::filesystem::remove(partial, ec);
        }
    };
    QStringList skipped;
    for (size_t i = 0; i < wanted.size(); ++i) {
        const std::optional<std::string> contents = zip.Read(*entries[i], [&](u64 received) {
            progress.setValue(static_cast<int>((done + received) * 1000 / std::max<u64>(total, 1)));
            QApplication::processEvents();
            return !progress.wasCanceled();
        });
        if (!contents) {
            discard();
            progress.close();
            if (!zip.canceled) {
                fail(zip.error);
            }
            return;
        }
        done += entries[i]->compressed;
        const std::filesystem::path target = folder / std::filesystem::path{wanted[i]}.filename();
        std::filesystem::path partial = target;
        partial += L".download";
        {
            std::ofstream file(partial, std::ios::binary | std::ios::trunc);
            file.write(contents->data(), static_cast<std::streamsize>(contents->size()));
            if (!file) {
                discard();
                progress.close();
                fail("could not write " + Common::FS::PathToUTF8String(partial));
                return;
            }
        }
        // Streamline's own modules (sl.*) carry Streamline's signature; NVIDIA's other DLLs an
        // Authenticode one.
        const bool streamline_module = FileName(wanted[i]).starts_with("sl.");
        const bool signed_ok = streamline_module ? Streamline::IsStreamlineModule(partial)
                                                 : Streamline::IsSignedByNvidia(partial);
        if (!signed_ok && in_frame_gen_set(wanted[i])) {
            std::filesystem::remove(partial, ec);
            LOG_WARNING(Frontend, "{} from NVIDIA's release is not signed by NVIDIA; skipping it",
                        FileName(wanted[i]));
            skipped << QString::fromStdString(FileName(wanted[i]));
            continue;
        }
        if (!signed_ok) {
            std::filesystem::remove(partial, ec);
            discard();
            progress.close();
            fail(Common::FS::PathToUTF8String(target.filename()) + " is not signed by NVIDIA");
            return;
        }
        verified.emplace_back(partial, target);
    }
    if (!skipped.isEmpty()) {
        std::erase_if(verified, [&](const auto& file) {
            if (!in_frame_gen_set(BIN_DIR + Common::FS::PathToUTF8String(file.second.filename()))) {
                return false;
            }
            std::filesystem::remove(file.first, ec);
            return true;
        });
    }
    // The old files move aside first, so a failed save puts every one of them back.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> replaced;
    const auto restore = [&replaced, &ec] {
        for (const auto& [backup, target] : replaced) {
            std::filesystem::remove(target, ec);
            if (!backup.empty()) {
                std::filesystem::rename(backup, target, ec);
            }
        }
    };
    for (const auto& [partial, target] : verified) {
        std::filesystem::path backup;
        if (std::filesystem::exists(target, ec)) {
            backup = target;
            backup += L".old";
            std::filesystem::remove(backup, ec);
            std::filesystem::rename(target, backup, ec);
        }
        bool failed = static_cast<bool>(ec);
        if (!failed) {
            std::filesystem::rename(partial, target, ec);
            failed = static_cast<bool>(ec);
            if (failed && !backup.empty()) {
                std::filesystem::rename(backup, target, ec);
            }
        }
        if (failed) {
            restore();
            discard();
            progress.close();
            fail("could not save " + Common::FS::PathToUTF8String(target));
            return;
        }
        replaced.emplace_back(backup, target);
    }
    for (const auto& [backup, target] : replaced) {
        if (!backup.empty()) {
            std::filesystem::remove(backup, ec);
        }
        LOG_INFO(Frontend, "Downloaded {} from NVIDIA's Streamline {} release",
                 Common::FS::PathToUTF8String(target), RELEASE_VERSION);
    }
    progress.setValue(1000);
    progress.close();
    Refresh();
    if (!skipped.isEmpty()) {
        QMessageBox::warning(this, Tr("NVIDIA DLSS"),
                             Tr("These files failed NVIDIA's signature check, so none of DLSS "
                                "Frame Generation's files were saved and it stays off: %1")
                                 .arg(skipped.join(QStringLiteral(", "))));
        return;
    }
    QMessageBox::information(this, Tr("NVIDIA DLSS"),
                             Tr("NVIDIA DLSS is ready. It is used the next time a game starts."));
#else
    const auto fail = [this](const std::string& error) {
        LOG_ERROR(Frontend, "NVIDIA DLSS download failed: {}", error);
        QMessageBox::warning(this, Tr("NVIDIA DLSS"),
                             Tr("Could not download the DLSS files: %1")
                                 .arg(QString::fromStdString(error)));
        Refresh();
    };
    const std::filesystem::path folder = Folder();

    // NVIDIA's licence first: nothing is fetched before it is accepted.
    QProgressDialog busy(Tr("Contacting the download..."), QString{}, 0, 0, this);
    busy.setWindowTitle(Tr("NVIDIA DLSS"));
    busy.setWindowModality(Qt::WindowModal);
    busy.setMinimumDuration(0);
    busy.show();
    QApplication::processEvents();
    std::string error;
    const auto module = LatestModule(error);
    if (!module) {
        busy.close();
        fail(error);
        return;
    }
    const std::vector<Downloadable> wanted = WantedFiles(folder, module->first, module->second);
    const Downloadable* licence_file = nullptr;
    for (const Downloadable& file : wanted) {
        if (file.target.filename() == "NVIDIA-LICENSE.txt") {
            licence_file = &file;
        }
    }
    std::string licence;
    if (!licence_file || !FetchFile(licence_file->url, {}, error, licence) ||
        !Sha256Matches(licence, licence_file->sha256)) {
        busy.close();
        fail(licence_file ? error : "no licence address");
        return;
    }
    busy.close();

    QStringList names;
    for (const Downloadable& file : wanted) {
        names << QString::fromStdString(file.target.filename().string());
    }
    QDialog dialog(this);
    dialog.setWindowTitle(Tr("Download NVIDIA DLSS"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* intro = new QLabel(
        Tr("Lichii will download %1 into %2 (about 57 MB).<br>Lichii's DLSS module is Lichii's "
           "own, under the MIT licence. NVIDIA's DLSS model and this licence text are NVIDIA's, "
           "under NVIDIA's licence terms below.")
            .arg(names.join(QStringLiteral(", ")), QString::fromStdString(folder.string())),
        &dialog);
    intro->setWordWrap(true);
    QPlainTextEdit* licence_text = new QPlainTextEdit(QString::fromStdString(licence), &dialog);
    licence_text->setReadOnly(true);
    licence_text->setMinimumSize(640, 380);
    QDialogButtonBox* buttons = new QDialogButtonBox(&dialog);
    buttons->addButton(Tr("Accept and Download"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(intro);
    layout->addWidget(licence_text, 1);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QProgressDialog progress(Tr("Downloading the DLSS files..."), Tr("Cancel"), 0, 1000, this);
    progress.setWindowTitle(Tr("NVIDIA DLSS"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    QApplication::processEvents();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    // Every file is fetched and checked before any is replaced, so a failed
    // download leaves the old files in place.
    std::vector<std::pair<std::string, std::string>> fetched;
    for (size_t i = 0; i < wanted.size(); ++i) {
        std::string data;
        if (!FetchFile(wanted[i].url,
                       [&](u64) {
                           QApplication::processEvents();
                           return !progress.wasCanceled();
                       },
                       error, data) ||
            !Sha256Matches(data, wanted[i].sha256)) {
            progress.close();
            if (!progress.wasCanceled()) {
                fail(error.empty() ? fmt::format("{} has a wrong checksum",
                                                 wanted[i].target.filename().string())
                                   : error);
            }
            return;
        }
        fetched.emplace_back(wanted[i].target.filename().string(), std::move(data));
        progress.setValue(static_cast<int>((i + 1) * 1000 / wanted.size()));
        QApplication::processEvents();
    }

    // The old files move aside first, so a failed save puts every one of them back.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> replaced;
    const auto restore = [&replaced, &ec] {
        for (const auto& [backup, target] : replaced) {
            std::filesystem::remove(target, ec);
            if (!backup.empty()) {
                std::filesystem::rename(backup, target, ec);
            }
        }
    };
    for (size_t i = 0; i < wanted.size(); ++i) {
        const std::filesystem::path& target = wanted[i].target;
        std::filesystem::path backup;
        if (std::filesystem::exists(target, ec)) {
            backup = target;
            backup += ".old";
            std::filesystem::remove(backup, ec);
            std::filesystem::rename(target, backup, ec);
        }
        bool failed = static_cast<bool>(ec);
        if (!failed) {
            std::ofstream file(target, std::ios::binary | std::ios::trunc);
            file.write(fetched[i].second.data(),
                       static_cast<std::streamsize>(fetched[i].second.size()));
            failed = !file;
        }
        if (failed) {
            restore();
            progress.close();
            fail("could not save " + Common::FS::PathToUTF8String(target));
            return;
        }
        replaced.emplace_back(backup, target);
    }
    for (const auto& [backup, target] : replaced) {
        if (!backup.empty()) {
            std::filesystem::remove(backup, ec);
        }
        LOG_INFO(Frontend, "Downloaded {}", Common::FS::PathToUTF8String(target));
    }
    progress.setValue(1000);
    progress.close();
    Refresh();
    // The release can be built for another ABI than this Lichii; the panel then says so.
    if (Vulkan::Ngx::Probe().setup == Vulkan::Ngx::Setup::WrongAbi) {
        QMessageBox::warning(this, Tr("NVIDIA DLSS"),
                             Tr("The latest DLSS module is from another Lichii version. Until "
                                "a matching one is released, games use AMD FSR 3.1."));
        return;
    }
    QMessageBox::information(this, Tr("NVIDIA DLSS"),
                             Tr("NVIDIA DLSS is ready. It is used the next time a game starts."));
#endif
}

#ifndef _WIN32
void NvidiaDlssPanel::InstallFromFile() {
    const QString picked = QFileDialog::getOpenFileName(
        this, Tr("Install Lichii's DLSS module"), QString{},
        QStringLiteral("lichii-dlss-module.so (lichii-dlss-module.so)"));
    if (picked.isEmpty()) {
        return;
    }
    const std::filesystem::path source = std::filesystem::path{picked.toStdString()}.parent_path();
    const std::filesystem::path folder = Folder();
    std::error_code ec;
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(source, ec)) {
        names.push_back(entry.path().filename().string());
    }
    const auto files = VideoCommon::DlssInstallFiles(names);
    if (!files) {
        QMessageBox::warning(this, Tr("NVIDIA DLSS"),
                             Tr("%1 needs NVIDIA's DLSS model (libnvidia-ngx-dlss.so.*) in the "
                                "same folder.")
                                 .arg(picked));
        return;
    }
    // Each file lands under a temporary name first, so a failed copy never leaves half a file.
    std::filesystem::create_directories(folder, ec);
    for (const std::string& name : *files) {
        const std::filesystem::path target = folder / name;
        std::filesystem::path part = target;
        part += ".part";
        std::filesystem::copy_file(source / name, part,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            std::filesystem::rename(part, target, ec);
        }
        if (ec) {
            std::filesystem::remove(part, ec);
            LOG_ERROR(Frontend, "NVIDIA DLSS install failed for {}", name);
            QMessageBox::warning(this, Tr("NVIDIA DLSS"),
                                 Tr("Could not copy %1 into %2.")
                                     .arg(QString::fromStdString(name),
                                          QString::fromStdString(folder.string())));
            Refresh();
            return;
        }
        LOG_INFO(Frontend, "Installed {}", Common::FS::PathToUTF8String(target));
    }
    Refresh();
    const Vulkan::Ngx::State state = Vulkan::Ngx::Probe();
    if (state.setup == Vulkan::Ngx::Setup::Ready) {
        QMessageBox::information(this, Tr("NVIDIA DLSS"),
                                 Tr("NVIDIA DLSS is ready. It is used the next time a game starts."));
    } else {
        QMessageBox::warning(this, Tr("NVIDIA DLSS"),
                             Tr("The files were copied, but DLSS is not ready: see the panel."));
    }
}
#endif

#endif
