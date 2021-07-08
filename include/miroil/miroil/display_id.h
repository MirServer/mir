#ifndef MIROIL_DISPLAY_ID_H
#define MIROIL_DISPLAY_ID_H

#include <mir/int_wrapper.h>

#include <miroil/edid.h>

namespace mir { namespace graphics { namespace detail { struct GraphicsConfOutputIdTag; } } }

namespace miroil
{
using OutputId = mir::IntWrapper<mir::graphics::detail::GraphicsConfOutputIdTag>;

struct DisplayId
{
    Edid edid;
    OutputId output_id; // helps to identify a monitor if we have two of the same.
};

} // namespace miral

#endif // MIROIL_DISPLAY_ID_H
