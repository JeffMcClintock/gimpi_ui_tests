// Pins the vtable layout of the drawing interfaces.
//
// IBitmapRenderTarget derives from IDeviceContext and declares exactly one
// virtual of its own, getBitmap, so getBitmap sits immediately after
// IDeviceContext's last method and moves down one slot every time a method is
// appended to IDeviceContext. A GUI module compiled against an older header
// then calls the newly appended method where it means to call getBitmap:
// wrong function, wrong arguments, a segfault inside the callee rather than a
// diagnosable error. It has happened twice (pushClipGeometry, drawTextLayout),
// both times a deliberate beta-period exception rather than an accident, and
// both times it crashed every prebuilt module that draws a drop shadow.
//
// The policy is not to break the vtable. This test is the tripwire that makes
// breaking it a build failure instead of a customer's crash report.
//
// Itanium ABI only (Linux, macOS): a pointer to a virtual member function
// encodes its byte offset into the vtable in the first word. MSVC uses thunks
// instead, so the check compiles away there — single-inheritance layout is the
// same on all three, so pinning two platforms pins the third.
//
// The exact encoding varies: x86-64 stores offset+1 and flags "virtual" in that
// low bit, while AAPCS64 (Apple silicon) stores the offset as-is and flags
// virtual in the adjustment word. Rather than branch on the platform and be
// wrong on the next one, calibrate: IUnknown::queryInterface is the first
// virtual of the root interface, so it is slot 0 by definition, and whatever
// raw value the compiler encodes for it is this encoding's bias.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "GmpiApiDrawing.h"

#if !defined(_MSC_VER)

namespace
{
// The raw first word of a pointer-to-member-function.
template <class MemberFunction>
std::uintptr_t rawMemberPointer(MemberFunction pmf)
{
    struct ItaniumMemberPointer
    {
        std::uintptr_t ptr;
        std::ptrdiff_t adjustment;
    } raw{};

    static_assert(sizeof(MemberFunction) == sizeof(raw),
                  "not the Itanium member-pointer representation this decodes");
    std::memcpy(&raw, &pmf, sizeof(raw));
    return raw.ptr;
}

// Slot 0 of the root interface: whatever it encodes to is this ABI's bias.
const std::uintptr_t encodingBias = rawMemberPointer(&gmpi::api::IUnknown::queryInterface);

template <class MemberFunction>
std::size_t vtableSlotOf(MemberFunction pmf)
{
    return (rawMemberPointer(pmf) - encodingBias) / sizeof(void*);
}

constexpr const char* kWhatToDo =
    "\nThe drawing ABI moved. Any GUI module built against the previous header "
    "now calls the wrong virtual and will segfault in a way that blames the "
    "module author.\n"
    "If you appended to IDeviceContext: don't — put new drawing entry points on "
    "IFactory (no derived interface) behind a queryInterface gate, or on a new "
    "interface.\n"
    "If the move is genuinely intended and every GUI module is being rebuilt in "
    "step, update the expected slot here in the same commit.";
} // namespace

// The canary. Appending to IDeviceContext moves this.
TEST(DrawingAbi, GetBitmapSlotIsPinned)
{
    EXPECT_EQ(vtableSlotOf(&gmpi::drawing::api::IBitmapRenderTarget::getBitmap),
              std::size_t{32})
        << kWhatToDo;
}

// An early anchor: if this moves too, something was inserted into IResource or
// IUnknown rather than appended further down, which shifts every interface in
// the header at once.
TEST(DrawingAbi, DeviceContextBaseSlotsArePinned)
{
    EXPECT_EQ(vtableSlotOf(&gmpi::drawing::api::IDeviceContext::getFactory),
              std::size_t{3})
        << kWhatToDo;
}

// Distinguishes the two failure modes: with these pinned, a getBitmap failure
// above means "appended after drawTextLayout", while these moving too means
// "inserted in the middle", which is worse and breaks every module.
TEST(DrawingAbi, DeliberatelyAppendedMethodsAreWhereTheyWereLeft)
{
    EXPECT_EQ(vtableSlotOf(&gmpi::drawing::api::IDeviceContext::pushClipGeometry),
              std::size_t{30})
        << kWhatToDo;
    EXPECT_EQ(vtableSlotOf(&gmpi::drawing::api::IDeviceContext::drawTextLayout),
              std::size_t{31})
        << kWhatToDo;
}

#endif // !_MSC_VER
