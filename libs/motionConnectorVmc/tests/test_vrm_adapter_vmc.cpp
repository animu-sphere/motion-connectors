// SPDX-License-Identifier: Apache-2.0
//
// Scaffold-stage tests: the diagnostic table is a contract before it has any
// caller, so it is tested before it has any caller.
#include "motionConnectorVmc/Diagnostics.h"

#include "motionCore/MotionPose.h"
#include "motionRecording/LiveCaptureSource.h"

#include <cassert>
#include <cstdio>
#include <locale>
#include <set>
#include <string>

namespace vmc = openstrata::connectors::vmc;

namespace
{

using vmc::Diagnostic;
using vmc::DiagnosticCode;
using vmc::DiagnosticCodeCount;
using vmc::DiagnosticSeverity;

// The eight codes roadmap/adapters-mocopi-vmc-ardy.md §8 assigns to this
// adapter, spelled exactly as that document spells them. This list is the
// reason to have a test at all: a renamed or dropped code is a contract break
// that nothing else in the tree would notice.
constexpr const char* kExpectedCodes[] = {
    "VMC_PACKET_MALFORMED",   "VMC_UNSUPPORTED_MESSAGE", "VMC_TIMESTAMP_REGRESSION",
    "VMC_DUPLICATE_BONE",     "VMC_INCOMPLETE_FRAME",    "VMC_SOURCE_RESTARTED",
    "VMC_SOCKET_BIND_FAILED", "VMC_STALE_JOINT",
};

void
TestEveryCodeIsNamedOnceAndRoundTrips()
{
    constexpr std::size_t expected = sizeof(kExpectedCodes) / sizeof(kExpectedCodes[0]);
    assert(DiagnosticCodeCount == expected);

    std::set<std::string> seen;
    for (std::size_t i = 0; i < DiagnosticCodeCount; ++i)
    {
        const auto code = static_cast<DiagnosticCode>(i);
        const std::string name(vmc::DiagnosticCodeString(code));

        assert(name == kExpectedCodes[i]);
        assert(seen.insert(name).second);

        const auto found = vmc::FindDiagnosticCode(name);
        assert(found && *found == code);
    }

    assert(!vmc::FindDiagnosticCode("VMC_NOT_A_CODE"));
    assert(!vmc::FindDiagnosticCode("VRM_VMC_PACKET_MALFORMED"));
    // The canonical layer's namespace is not this adapter's to emit (§8).
    assert(!vmc::FindDiagnosticCode("VRM_MOTION_SAMPLE_STALE"));
}

void
TestOnlyABindFailureStopsTheSession()
{
    // The recoverable flag is what lets a caller distinguish a live session
    // that can continue from one that cannot, so exactly one code is fatal:
    // a receiver that never bound has nothing to recover into.
    for (std::size_t i = 0; i < DiagnosticCodeCount; ++i)
    {
        const auto code = static_cast<DiagnosticCode>(i);
        const bool fatal = code == DiagnosticCode::SocketBindFailed;
        assert(vmc::DiagnosticIsRecoverable(code) == !fatal);
        assert((vmc::DiagnosticDefaultSeverity(code) == DiagnosticSeverity::Error) ==
               fatal);
    }
}

void
TestMakeDiagnosticCannotDisagreeWithTheTable()
{
    const Diagnostic stale =
        vmc::MakeDiagnostic(DiagnosticCode::StaleJoint, "no update for 0.5 s");
    assert(stale.severity == DiagnosticSeverity::Warning);
    assert(stale.recoverable);
    assert(stale.detail == "no update for 0.5 s");
    assert(!stale.timestamp);
    assert(!stale.sequence);
}

void
TestFormattingIsDeterministicAndOmitsAbsentFields()
{
    Diagnostic full =
        vmc::MakeDiagnostic(DiagnosticCode::StaleJoint, "no update for 0.5 s");
    full.source = "127.0.0.1:39539";
    full.timestamp = 1.5;
    // A humanoid bone name, spelled the way motionCore spells it -- the adapter
    // reports semantics, never a target joint index (§5.1).
    full.subject = std::string(openstrata::motion::HumanJointName(openstrata::motion::HumanJoint::LeftHand));
    full.sequence = 42;

    assert(vmc::FormatDiagnostic(full) ==
           "[VMC_STALE_JOINT] warning recoverable source=127.0.0.1:39539"
           " t=1.500000 subject=leftHand seq=42: no update for 0.5 s");

    const Diagnostic bare = vmc::MakeDiagnostic(DiagnosticCode::SocketBindFailed);
    assert(vmc::FormatDiagnostic(bare) == "[VMC_SOCKET_BIND_FAILED] error fatal");
}

// A locale whose decimal point is a comma, constructed in-process so this test
// depends on no system locale being installed anywhere.
struct CommaDecimalPoint : std::numpunct<char>
{
  protected:
    char
    do_decimal_point() const override
    {
        return ',';
    }
};

void
TestFormattingSurvivesAHostileGlobalLocale()
{
    // A default-constructed ostringstream is imbued with the *global* locale,
    // so a host that installs one — a DCC calling setlocale is the realistic
    // case — would otherwise turn `t=1.500000` into `t=1,500000` and make a
    // diagnostic disagree with the capture trace it refers to.
    Diagnostic pinned = vmc::MakeDiagnostic(DiagnosticCode::TimestampRegression);
    pinned.timestamp = 1.5;

    const std::locale previous =
        std::locale::global(std::locale(std::locale::classic(), new CommaDecimalPoint));
    const std::string formatted = vmc::FormatDiagnostic(pinned);
    std::locale::global(previous);

    assert(formatted == "[VMC_TIMESTAMP_REGRESSION] warning recoverable t=1.500000");
}

void
TestTheDeclaredDependencyEdgesAreReal()
{
    // Both of the two edges this adapter's manifest declares -- and the only
    // two WORKSPACE.md §2 permits it -- are exercised here, so the manifest
    // cannot claim a dependency the library does not actually have.
    openstrata::motion::LiveCaptureSource source;
    openstrata::motion::MotionPose pose;
    pose.timestamp = 0.0;
    pose.validRotations.set(static_cast<std::size_t>(openstrata::motion::HumanJoint::Hips));

    assert(source.Push(pose));
    assert(!source.IsEmpty());
}

} // namespace

int
main()
{
    TestEveryCodeIsNamedOnceAndRoundTrips();
    TestOnlyABindFailureStopsTheSession();
    TestMakeDiagnosticCannotDisagreeWithTheTable();
    TestFormattingIsDeterministicAndOmitsAbsentFields();
    TestFormattingSurvivesAHostileGlobalLocale();
    TestTheDeclaredDependencyEdgesAreReal();
    std::puts("motionConnectorVmc unit tests passed");
    return 0;
}
