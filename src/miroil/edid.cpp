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
 * Authored by: Nick Dedekind <nick.dedekind@canonical.com>
 */

#include "miroil/edid.h"

#include <cstring>
#include <numeric>
#include <stdexcept>

miroil::Edid& miroil::Edid::parse_data(std::vector<uint8_t> const& data)
{
    if (data.size() != 128 && data.size() != 256) {
        throw std::runtime_error("Incorrect EDID structure size");
    }

    // check the checksum
    uint8_t sum = std::accumulate(data.begin(), data.end(), 0);
    if (sum != 0) {
        throw std::runtime_error("Invalid EDID checksum");
    }

    // check header
    static constexpr uint8_t sig[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
    if (std::memcmp(data.data(), sig, sizeof sig)) {
        throw std::runtime_error("Invalid EDID header");
    }

    vendor = { (char)((data[8] >> 2 & 0x1f) + 'A' - 1),
               (char)((((data[8] & 0x3) << 3) | ((data[9] & 0xe0) >> 5)) + 'A' - 1),
               (char)((data[9] & 0x1f) + 'A' - 1) };

    product_code = *(uint16_t*)&data[10];
    serial_number = *(uint32_t*)&data[12];

    size.width = (int)data[21] * 10;
    size.height = (int)data[22] * 10;

    for (int descriptor_index = 0, i = 54; descriptor_index < 4; descriptor_index++, i += 18) { //read through descriptor blocks... 54-71, 72-89, 90-107, 108-125
        if (data[i] == 0x00) { //not a timing descriptor

            auto& descriptor = descriptors[descriptor_index];
            auto& value = descriptor.value;

            descriptor.type = static_cast<Edid::Descriptor::Type>(data[i+3]);

            switch (descriptor.type) {
            case Descriptor::Type::monitor_name:
                for (int j = 5; j < 18; j++) {
                    if (data[i+j] == 0x0a) {
                        break;
                    } else {
                        value.monitor_name[j-5] = data[i+j];
                    }
                }
                break;
            case Descriptor::Type::monitor_limits:
                break;
            case Descriptor::Type::unspecified_text:
                for (int j = 5; j < 18; j++) {
                    if (data[i+j] == 0x0a) {
                        break;
                    } else {
                        value.unspecified_text[j-5] = data[i+j];
                    }
                }
                break;
            case Descriptor::Type::serial_number:
                for (int j = 5; j < 18; j++) {
                    if (data[i+j] == 0x0a) {
                        break;
                    } else {
                        value.serial_number[j-5] = data[i+j];
                    }
                }
                break;
            default:
                break;
            }
        }
    }

    return *this;
}

std::string miroil::Edid::Descriptor::string_value() const
{
    switch(type) {
    case Type::monitor_name:
        return std::string(value.monitor_name);
    case Type::unspecified_text:
        return std::string(value.unspecified_text);
    case Type::serial_number:
        return std::string(value.serial_number);
    default:
        return {};
    }
}
