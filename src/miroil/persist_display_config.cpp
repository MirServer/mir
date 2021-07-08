/*
 * Copyright © 2016-2020 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "miroil/persist_display_config.h"
#include "miroil/display_configuration_policy.h"
#include "miroil/display_configuration_storage.h"

#include <mir/graphics/display_configuration.h>
#include <mir/graphics/display_configuration_observer.h>
#include <mir/graphics/display_configuration_policy.h>
#include <mir/observer_registrar.h>
#include <mir/server.h>
#include <mir/log.h>

#include <boost/throw_exception.hpp>

#include <stdexcept>

namespace mg = mir::graphics;

namespace
{
struct PersistDisplayConfigPolicy
{
    PersistDisplayConfigPolicy(std::shared_ptr<miroil::DisplayConfigurationStorage> const& storage) :
        storage(storage) {}
    virtual ~PersistDisplayConfigPolicy() = default;
    PersistDisplayConfigPolicy(PersistDisplayConfigPolicy const&) = delete;
    auto operator=(PersistDisplayConfigPolicy const&) -> PersistDisplayConfigPolicy& = delete;

    void apply_to(mg::DisplayConfiguration& conf,
                  miroil::DisplayConfigurationPolicy& wrapped_policy,
                  miroil::DisplayConfigurationPolicy& custom_policy);
    void save_config(mg::DisplayConfiguration const& base_conf);

    std::shared_ptr<miroil::DisplayConfigurationStorage> storage;
};

struct MiralWrappedMirDisplayConfigurationPolicy : miroil::DisplayConfigurationPolicy
{
    MiralWrappedMirDisplayConfigurationPolicy(std::shared_ptr<mg::DisplayConfigurationPolicy> const& self) :
        self{self}
    {}

    void apply_to(mg::DisplayConfiguration& conf) override
    {
        self->apply_to(conf);
    }

    std::shared_ptr<mg::DisplayConfigurationPolicy> self;
};

struct DisplayConfigurationPolicyAdapter : mg::DisplayConfigurationPolicy
{
    DisplayConfigurationPolicyAdapter(
        std::shared_ptr<PersistDisplayConfigPolicy> const& self,
            std::shared_ptr<miroil::DisplayConfigurationPolicy> const& wrapped_policy,
            std::shared_ptr<miroil::DisplayConfigurationPolicy> const& custom_policy) :
        self{self}, wrapped_policy{wrapped_policy}, custom_policy{custom_policy}
    {}

    void apply_to(mg::DisplayConfiguration& conf) override
    {
        self->apply_to(conf, *wrapped_policy, *custom_policy);
    }

    std::shared_ptr<PersistDisplayConfigPolicy> const self;
    std::shared_ptr<miroil::DisplayConfigurationPolicy> const wrapped_policy;
    std::shared_ptr<miroil::DisplayConfigurationPolicy> const custom_policy;
};

struct DisplayConfigurationObserver : mg::DisplayConfigurationObserver
{
    void initial_configuration(std::shared_ptr<mg::DisplayConfiguration const> const& /*config*/) override {}

    void configuration_applied(std::shared_ptr<mg::DisplayConfiguration const> const& /*config*/) override {}

    void configuration_failed(
        std::shared_ptr<mg::DisplayConfiguration const> const& /*attempted*/,
        std::exception const& /*error*/) override {}

    void catastrophic_configuration_error(
        std::shared_ptr<mg::DisplayConfiguration const> const& /*failed_fallback*/,
        std::exception const& /*error*/) override {}

    void configuration_updated_for_session(
        std::shared_ptr<mir::scene::Session> const&,
        std::shared_ptr<mg::DisplayConfiguration const> const&) override {}
};

miroil::DisplayConfigurationOptions::DisplayMode current_mode_of(mg::DisplayConfigurationOutput const& output)
{
    miroil::DisplayConfigurationOptions::DisplayMode mode;
    if (output.current_mode_index >= output.modes.size()) return mode;

    auto const& current_mode = output.modes[output.current_mode_index];
    mode.size = current_mode.size;
    mode.refresh_rate = current_mode.vrefresh_hz;
    return mode;
}

// From Qt
inline bool qFuzzyCompare(double p1, double p2)
{
    return (std::abs(p1 - p2) * 1000000000000. <= std::min(std::abs(p1), std::abs(p2)));
}
}

struct miroil::PersistDisplayConfig::Self : PersistDisplayConfigPolicy, DisplayConfigurationObserver
{
    Self(std::shared_ptr<DisplayConfigurationStorage> const& storage,
         DisplayConfigurationPolicyWrapper const& custom_wrapper) :
        PersistDisplayConfigPolicy(storage),
        custom_wrapper{custom_wrapper} {}

    DisplayConfigurationPolicyWrapper const custom_wrapper;

    void base_configuration_updated(std::shared_ptr<mg::DisplayConfiguration const> const& base_config) override
    {
        save_config(*base_config);
    }

    void session_configuration_applied(std::shared_ptr<mir::scene::Session> const&,
                                       std::shared_ptr<mg::DisplayConfiguration> const&) override {}
    void session_configuration_removed(std::shared_ptr<mir::scene::Session> const&) override {}
    void configuration_updated_for_session(
        std::shared_ptr<mir::scene::Session> const&,
        std::shared_ptr<mg::DisplayConfiguration const> const&) override {}
};

miroil::PersistDisplayConfig::PersistDisplayConfig(std::shared_ptr<DisplayConfigurationStorage> const& storage,
                                                  DisplayConfigurationPolicyWrapper const& custom_wrapper) :
    self{std::make_shared<Self>(storage, custom_wrapper)}
{
}

miroil::PersistDisplayConfig::~PersistDisplayConfig() = default;

miroil::PersistDisplayConfig::PersistDisplayConfig(PersistDisplayConfig const&) = default;

auto miroil::PersistDisplayConfig::operator=(PersistDisplayConfig const&) -> PersistDisplayConfig& = default;

void miroil::PersistDisplayConfig::operator()(mir::Server& server)
{
    server.wrap_display_configuration_policy(
        [this](std::shared_ptr<mg::DisplayConfigurationPolicy> const& wrapped)
        -> std::shared_ptr<mg::DisplayConfigurationPolicy>
        {
            auto custom_wrapper = self->custom_wrapper(wrapped);
            return std::make_shared<DisplayConfigurationPolicyAdapter>(self,
                                                                       std::make_shared<MiralWrappedMirDisplayConfigurationPolicy>(wrapped),
                                                                       custom_wrapper);
        });

    server.add_init_callback([this, &server]
        { server.the_display_configuration_observer_registrar()->register_interest(self); });
}

void PersistDisplayConfigPolicy::apply_to(
    mg::DisplayConfiguration& conf,
    miroil::DisplayConfigurationPolicy& wrapped_policy,
    miroil::DisplayConfigurationPolicy& custom_policy)
{
    // first apply the policy we wrapped by setting a custom policy
    wrapped_policy.apply_to(conf);
    // then apply the custom policy
    custom_policy.apply_to(conf);

    if (!storage) {
        BOOST_THROW_EXCEPTION(std::runtime_error("No display configuration storage supplied."));
    }

    conf.for_each_output([this, &conf](mg::UserDisplayConfigurationOutput& output) {
        if (!output.connected) return;

        try {
            miroil::DisplayId display_id;
            // FIXME - output.edid should be std::vector<uint8_t>, not std::vector<uint8_t const>
            display_id.edid.parse_data(reinterpret_cast<std::vector<uint8_t> const&>(output.edid));
            display_id.output_id = output.id;

            // TODO if the h/w profile (by some definition) has changed, then apply corresponding saved config (if any).
            // TODO Otherwise...

            miroil::DisplayConfigurationOptions config;
            if (storage->load(display_id, config)) {

                if (config.mode.is_set()) {
                    int mode_index = output.current_mode_index;
                    int i = 0;
                    // Find the mode index which supports the saved size.
                    for (auto iter = output.modes.cbegin(); iter != output.modes.cend(); ++iter, i++) {
                        auto const& mode = *iter;
                        auto const& newMode = config.mode.value();
                        if (mode.size == newMode.size && qFuzzyCompare(mode.vrefresh_hz, newMode.refresh_rate)) {
                            mode_index = i;
                            break;
                        }
                    }
                    output.current_mode_index = mode_index;
                }

                uint output_index = 0;
                conf.for_each_output([&output, config, &output_index](mg::DisplayConfigurationOutput const& find_output) {
                    if (output_index == config.clone_output_index.value()) {
                        output.top_left = find_output.top_left;
                    }
                    output_index++;
                });

                if (config.orientation.is_set()) {output.orientation = config.orientation.value(); }
                if (config.used.is_set()) {output.used = config.used.value(); }
                if (config.form_factor.is_set()) {output.form_factor = config.form_factor.value(); }
                if (config.scale.is_set()) {output.scale = config.scale.value(); }
            }
        } catch (std::runtime_error const& e) {
            mir::log_info("Failed to parse EDID - %s\n", e.what());
        }
    });
}

void PersistDisplayConfigPolicy::save_config(mg::DisplayConfiguration const& conf)
{
    if (!storage) return;

    conf.for_each_output([this, &conf](mg::DisplayConfigurationOutput const& output) {
        if (!output.connected) return;

        try {
            miroil::DisplayId display_id;
            // FIXME - output.edid should be std::vector<uint8_t>, not std::vector<uint8_t const>
            display_id.edid.parse_data(reinterpret_cast<std::vector<uint8_t> const&>(output.edid));
            display_id.output_id = output.id;

            miroil::DisplayConfigurationOptions config;

            uint output_index = 0;
            conf.for_each_output([output, &config, &output_index](mg::DisplayConfigurationOutput const& find_output) {
                if (!config.clone_output_index.is_set() && output.top_left == find_output.top_left) {
                    config.clone_output_index = output_index;
                }
                output_index++;
            });

            config.mode = current_mode_of(output);
            config.form_factor = output.form_factor;
            config.orientation = output.orientation;
            config.scale = output.scale;
            config.used = output.used;

            storage->save(display_id, config);
        } catch (std::runtime_error const& e) {
            mir::log_info("Failed to parse EDID - %s\n", e.what());
        }
    });
}
