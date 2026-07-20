#pragma once
#include <string_view>

/**
 * Flow-control actions shared by CSG traversal and photon propagation.
 *
 * FlowAction is returned by routines that advance either a CSG tree traversal
 * or a photon propagation step. The caller uses the action to decide whether
 * to continue the current loop, terminate it, or process a reached geometry
 * boundary. Keeping the action as a scoped enum prevents it from being mixed
 * accidentally with unrelated integer flags, indices, or error codes.
 *
 * The explicit unsigned underlying type preserves the compact representation
 * previously used by the unscoped enum. flow_action_name is a constexpr,
 * allocation-free formatter intended for diagnostics; it returns "UNKNOWN"
 * for values outside the defined enumeration.
 */
enum class FlowAction : unsigned
{
    Undefined, ///< No action has been selected.
    Break,     ///< Stop the current traversal or propagation loop.
    Continue,  ///< Continue the current traversal or propagation loop.
    Boundary,  ///< A geometry boundary was reached and needs handling.
    Pass,      ///< Pass control to the next processing stage.
    Start,     ///< Initial action before the first processing step.
    Return,    ///< Return control to the caller.
    Last       ///< Sentinel one past the final actionable value.
};

/**
 * Returns the stable diagnostic name of a flow-control action.
 *
 * @param action Flow-control action to format.
 * @return Uppercase enumerator name, or "UNKNOWN" for an invalid value.
 */
constexpr std::string_view flow_action_name(FlowAction action) noexcept
{
    switch (action)
    {
    case FlowAction::Undefined:
        return "UNDEFINED";
    case FlowAction::Break:
        return "BREAK";
    case FlowAction::Continue:
        return "CONTINUE";
    case FlowAction::Boundary:
        return "BOUNDARY";
    case FlowAction::Pass:
        return "PASS";
    case FlowAction::Start:
        return "START";
    case FlowAction::Return:
        return "RETURN";
    case FlowAction::Last:
        return "LAST";
    }
    return "UNKNOWN";
}
