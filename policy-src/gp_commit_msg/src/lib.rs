//! Aram policy: every change is described with the project commit template.
//!
//!   GP-COMMIT-001  the title (`--intent`) starts with `feature:` or `bug:`
//!                  (optionally `feature(scope):` / `bug(scope):`) followed by text
//!   GP-COMMIT-002  the description (`--why`) has at least 20 characters
//!
//! "What was tested" (`--test-note`), the description's presence and the title
//! length are enforced at the seal by the `[commit]` template in nool.toml
//! (required trailers `Nool-Why`, `Nool-Test-Note`).
//!
//! Nool 8.0.2 supplies the intent projection only once the intent record
//! exists (after the seal), so these rules bite at `pre-push`: a knot with a
//! malformed title lands locally but cannot be pushed. Knots sealed before
//! this policy was pinned are grandfathered by id.
#![allow(unsafe_op_in_unsafe_fn)]

use nool_aram_policy_sdk::{Policy, PolicyContext, PolicyResult, policy};

pub const MIN_WHY_CHARS: usize = 20;
pub const TYPES: [&str; 2] = ["feature", "bug"];
/// Intents Nool writes itself when it lands governance changes.
const SYSTEM_PREFIXES: [&str; 2] = ["Policy set:", "nool init:"];
/// Hex ids of file-changing knots landed before this policy was pinned.
const GRANDFATHERED: [&str; 1] = [
    "d76939ff9fbe441a91afaa66beea6ef33ea2ab56d36135d9d2d36c60f008d541",
];

fn hex_id(id: &[u8; 32]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::with_capacity(64);
    for b in id {
        out.push(HEX[(b >> 4) as usize] as char);
        out.push(HEX[(b & 15) as usize] as char);
    }
    out
}
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

    fn evaluate(ctx: &PolicyContext) -> PolicyResult {
        let id = hex_id(&ctx.knot.header.knot_id);
        if GRANDFATHERED.contains(&id.as_str()) {
            return PolicyResult::allow();
        }
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
        assert!(check("nool init: record Nool's configuration files", "").is_ok());
    }

    #[test]
    fn rejects_bad_titles() {
        for t in ["add hat", "feat: x", "chore: x", "feature:", "feature:   ", "bug(): x", "Feature: x", "fix bug: x"] {
            assert_eq!(check(t, "a perfectly long description").unwrap_err().0, "GP-COMMIT-001", "{t}");
        }
    }

    #[test]
    fn hex_id_formats_lowercase() {
        let mut id = [0u8; 32];
        id[0] = 0xd7;
        id[31] = 0x41;
        let h = hex_id(&id);
        assert_eq!(h.len(), 64);
        assert!(h.starts_with("d7") && h.ends_with("41"));
    }

    #[test]
    fn rejects_short_why() {
        assert_eq!(check("bug: x", "too short").unwrap_err().0, "GP-COMMIT-002");
        assert_eq!(check("bug: x", "   ").unwrap_err().0, "GP-COMMIT-002");
    }
}
