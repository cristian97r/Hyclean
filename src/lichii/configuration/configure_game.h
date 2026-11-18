// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "common/common_types.h"
#include "common/settings_common.h"
#include "lichii/configuration/configuration_shared.h"

namespace Core {
class System;
}

namespace ConfigurationShared {
class Builder;
}

// Settings that belong to one game, shown only in that game's properties.
class ConfigureGame : public ConfigurationShared::Tab {
    Q_OBJECT

public:
    // The category with this title's own settings, if it has any.
    static std::optional<Settings::Category> CategoryFor(u64 title_id);

    explicit ConfigureGame(const Core::System& system_, Settings::Category category,
                           std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                           const ConfigurationShared::Builder& builder, QWidget* parent = nullptr);
    ~ConfigureGame() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override {}

private:
    const Core::System& system;
    std::vector<std::function<void(bool)>> apply_funcs;
};
