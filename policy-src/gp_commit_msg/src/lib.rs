//! Aram policy: every change is described with the project commit template.
//!
//!   GP-COMMIT-001  the title (`--intent`) starts with `feature:` or `bug:`
//!                  (optionally `feature(scope):` / `bug(scope):`) followed by text
//!   GP-COMMIT-002  the description (`--why`) has at least 20 characters
//!
//! "What was tested" (`--test-note`) and the title length are enforced by the
//! `[commit]` template in nool.toml (required trailer `Nool-Test-Note`).
#![allow(unsafe_op_in_unsafe_fn)]

use nool_aram_policy_sdk::{Policy, PolicyContext, PolicyResult, policy};

pub const MIN_WHY_CHARS: usize = 20;
pub const TYPES: [&str; 2] = ["feature", "bug"];
/// Intents Nool writes itself when it lands governance changes.
const SYSTEM_PREFIXES: [&str; 1] = ["Policy set:"];
const INSUFFICIENT_INTENT: u32 = 402;

const TEMPLATE_HELP: &str = "use: nool propose --intent \"feature|bug: <title>\" \
--why \"<description, 20+ chars>\" --test-note \"<what was tested>\"";

/// Returns the change type when `title` follows `type[(scope)]: text`.
pub fn change_type(title: &str) -> Option<&'static str> {
    let (head, rest) = title.split_once(':')?;
    if rest.trim().is_empty() {
        return None;
    }
    let base = match head.split_once('(') {
        Some((b, scope)) => {
            let inner = scope.strip_suffix(')')?;
            if inner.trim().is_empty() || inner.contains(['(', ')']) {
                return None;
            }
            b
        }
        None => head,
    };
    TYPES.iter().copied().find(|t| *t == base)
}

/// Checks a title and rationale; `Err((rule, message))` on violation.
pub fn check(title: &str, rationale: &str) -> Result<(), (&'static str, String)> {
    let title = title.trim();
    if SYSTEM_PREFIXES.iter().any(|p| title.starts_with(p)) {
        return Ok(());
    }
    if change_type(title).is_none() {
        return Err((
            "GP-COMMIT-001",
            format!("title \"{title}\" must start with \"feature:\" or \"bug:\""),
        ));
    }
    let why = rationale.trim().chars().count();
    if why < MIN_WHY_CHARS {
        return Err((
            "GP-COMMIT-002",
            format!("description (--why) has {why} characters; at least {MIN_WHY_CHARS} required"),
        ));
    }
    Ok(())
}

struct GpCommitMsg;

policy!(GpCommitMsg);

impl Policy for GpCommitMsg {
    fn name() -> &'static str {
        "gp_commit_msg"
    }

    fn evaluate(_ctx: &PolicyContext) -> PolicyResult {
        // Only file-changing proposals carry a commit template; task, tag and
        // other bookkeeping knots are out of scope.
        let touches_files = PolicyContext::diff_projection().is_some_and(|d| d.files_changed > 0);
        let Some(intent) = PolicyContext::intent_projection() else {
            return PolicyResult::allow();
        };
        if !touches_files {
            return PolicyResult::allow();
        }
        match check(&intent.title, &intent.rationale) {
            Ok(()) => PolicyResult::allow(),
            Err((rule, msg)) => PolicyResult::deny_with_evidence(
                INSUFFICIENT_INTENT,
                format!("[{rule}] {msg} ({TEMPLATE_HELP})"),
                format!("[{rule}] {msg}"),
            ),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_template_titles() {
        assert_eq!(change_type("feature: add hat switch"), Some("feature"));
        assert_eq!(change_type("bug(usb): stall on qualifier"), Some("bug"));
        assert!(check("feature: add hat", "Players need eight-way d-pad input").is_ok());
        assert!(check("Policy set: pin the policy set", "").is_ok());
    }

    #[test]
    fn rejects_bad_titles() {
        for t in ["add hat", "feat: x", "chore: x", "feature:", "feature:   ", "bug(): x", "Feature: x", "fix bug: x"] {
            assert_eq!(check(t, "a perfectly long description").unwrap_err().0, "GP-COMMIT-001", "{t}");
        }
    }

    #[test]
    fn rejects_short_why() {
        assert_eq!(check("bug: x", "too short").unwrap_err().0, "GP-COMMIT-002");
        assert_eq!(check("bug: x", "   ").unwrap_err().0, "GP-COMMIT-002");
    }
}
