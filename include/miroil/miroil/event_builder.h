/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MIROIL_EVENT_BUILDER_H
#define MIROIL_EVENT_BUILDER_H
#include <miral/toolkit_event.h>
#include <mir_toolkit/mir_input_device_types.h>
#include <chrono>
#include <memory>
#include <sys/types.h>
#include <vector>

struct MirEvent;
struct MirInputEvent;

namespace mir { typedef std::unique_ptr<MirEvent, void(*)(MirEvent*)> EventUPtr; }

namespace miroil
{
    
/*
    Creates Mir input events out of Qt input events
    
    The class is splitt into miroil::EventBuilder which does the internal mir stuff, 
    and qtmir::EventBuilder which handles the qt stuff. 

    One important feature is that it's able to match a QInputEvent with the MirInputEvent that originated it, so
    it can make a MirInputEvent version of a QInputEvent containing also information that the latter does not carry,
    such as relative axis movement for pointer devices.
*/
    
class EventBuilder {
    
public:
    class EventInfo {
    public:
        void store(const MirInputEvent *mirInputEvent, ulong qtTimestamp);
        
        ulong timestamp;
        MirInputDeviceId device_id;
        std::vector<uint8_t> cookie;
        float relative_x{0};
        float relative_y{0};
    };
    
public:
    EventBuilder();
    virtual ~EventBuilder();
    
    // add Touch event
    void add_touch(MirEvent &event, MirTouchId touch_id, MirTouchAction action,
        MirTouchTooltype tooltype, float x_axis_value, float y_axis_value,
        float pressure_value, float touch_major_value, float touch_minor_value, float size_value);
    
    // Key event
    mir::EventUPtr make_key_event(MirInputDeviceId device_id, std::chrono::nanoseconds timestamp,
        std::vector<uint8_t> const& cookie, MirKeyboardAction action, xkb_keysym_t key_code,
        int scan_code, MirInputEventModifiers modifiers);

    // Touch event
    mir::EventUPtr make_touch_event(MirInputDeviceId device_id, std::chrono::nanoseconds timestamp,
        std::vector<uint8_t> const& mac, MirInputEventModifiers modifiers);

    // Pointer event
    mir::EventUPtr make_pointer_event(MirInputDeviceId device_id, std::chrono::nanoseconds timestamp,
        std::vector<uint8_t> const& mac, MirInputEventModifiers modifiers, MirPointerAction action,
        MirPointerButtons buttons_pressed,
        float x_axis_value, float y_axis_value,
        float hscroll_value, float vscroll_value,
        float relative_x_value, float relative_y_value);
    
    EventInfo * find_info(ulong qtTimestamp);    
        
    /* Stores information that cannot be carried by QInputEvents so that it can be fully
       reconstructed later given the same qtTimestamp */
    void store(const MirInputEvent *mirInputEvent, ulong qtTimestamp);

private:
    /*
      Ring buffer that stores information on recent MirInputEvents that cannot be carried by QInputEvents.

      When MirInputEvents are dispatched through a QML scene, not all of its information can be carried
      by QInputEvents. Some information is lost. Thus further on, if we want to transform a QInputEvent back into
      its original MirInputEvent so that it can be consumed by a mir::scene::Surface and properly handled by mir clients
      we have to reach out to this EventRegistry to get the missing bits.

      Given the objective of this EventRegistry (MirInputEvent reconstruction after having gone through QQuickWindow input dispatch
      as a QInputEvent), it stores information only about the most recent MirInputEvents.
     */
    std::vector<EventInfo> event_info_vector;
    size_t next_index{0};
    size_t event_info_count{0};
};

}

#endif // MIROIL_EVENT_BUILDER_H
