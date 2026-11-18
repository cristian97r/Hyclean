// SPDX-License-Identifier: GPL-2.0-or-later

#include <map>
#include <QVBoxLayout>
#include "common/settings.h"
#include "core/core.h"
#include "lichii/configuration/configure_game.h"
#include "lichii/configuration/shared_widget.h"

std::optional<Settings::Category> ConfigureGame::CategoryFor(u64 title_id) {
    switch (title_id) {
    case 0x0400486024DEC000: // The Legend of Zelda: Ocarina of Time
        return Settings::Category::GameOcarina;
    default:
        return std::nullopt;
    }
}

ConfigureGame::ConfigureGame(const Core::System& system_, Settings::Category category,
                             std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                             const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group, parent), system{system_} {
    auto* const layout = new QVBoxLayout(this);
    std::map<u32, QWidget*> sorted;
    for (auto* const setting : Settings::values.linkage.by_category[category]) {
        auto* const widget = builder.BuildWidget(setting, apply_funcs);
        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }
        sorted.emplace(setting->Id(), widget);
    }
    for (const auto& [id, widget] : sorted) {
        layout->addWidget(widget);
    }
    layout->addStretch();
}

ConfigureGame::~ConfigureGame() = default;

void ConfigureGame::ApplyConfiguration() {
    const bool is_powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(is_powered_on);
    }
}
