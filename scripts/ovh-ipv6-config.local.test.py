#!/usr/bin/env python3
"""Tests for the egress-firewall audit in ovh-ipv6-config.local.py.

Most of that audit only executes on the server it was written for: it reads `/etc/csf/csf.conf`
and shells out to `ip6tables`, neither of which exists on a developer Mac. The branches that
matter most — the ones that fired on emule-qt.org and produced a wrong verdict — are therefore
the ones a plain run can never reach.

These tests feed the parsing and interpretation functions the rule shapes taken verbatim from
that server, so the logic stays covered wherever this is run.

    python3 scripts/ovh-ipv6-config.local.test.py
"""

from __future__ import annotations

import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(HERE, "ovh-ipv6-config.local.py")

# The module name must be registered before exec: @dataclass resolves the defining module out of
# sys.modules, and raises AttributeError on None if it is absent.
_spec = importlib.util.spec_from_file_location("ovh_ipv6_config", TARGET)
ovh = importlib.util.module_from_spec(_spec)
sys.modules["ovh_ipv6_config"] = ovh
_spec.loader.exec_module(ovh)

# Verbatim from `ip6tables -S` on emule-qt.org, including the root exemption that made a manual
# `nc` test succeed while PHP failed.
SERVER_RULES = [
    "-P OUTPUT DROP",
    "-A ALLOWOUT ! -o lo -p tcp -m owner --uid-owner 0 -j ACCEPT",
    "-A ALLOWOUT ! -o lo -p tcp -m tcp --dport 443 -j ACCEPT",
    "-A ALLOWOUT ! -o lo -p udp -m multiport --dports 53,123 -j ACCEPT",
    "-A LOGDROPOUT -j REJECT --reject-with icmp6-port-unreachable",
]

_passed = 0
_failed = 0


def check(name: str, condition: bool) -> None:
    global _passed, _failed
    print(("PASS: " if condition else "FAIL: ") + name)
    if condition:
        _passed += 1
    else:
        _failed += 1


def test_port_lists() -> None:
    check("range 1:65535 covers 5662", ovh.port_in_ranges(5662, ovh.parse_port_list("1:65535")))
    check("list excludes 5662", not ovh.port_in_ranges(5662, ovh.parse_port_list("20,21,443")))
    check("range lower bound", ovh.port_in_ranges(35000, ovh.parse_port_list("35000:35999")))
    check("range upper bound", ovh.port_in_ranges(35999, ovh.parse_port_list("35000:35999")))
    check("just past the range", not ovh.port_in_ranges(36000, ovh.parse_port_list("35000:35999")))
    check("empty list matches nothing", not ovh.port_in_ranges(80, ovh.parse_port_list("")))
    check("malformed entry skipped", ovh.port_in_ranges(80, ovh.parse_port_list("abc,80")))
    check("whitespace tolerated", ovh.port_in_ranges(80, ovh.parse_port_list(" 20 , 80 ")))


def test_rule_heuristic() -> None:
    check("--dport matched", ovh._rules_accept_port(SERVER_RULES, "tcp", 443))
    check("--dports matched", ovh._rules_accept_port(SERVER_RULES, "udp", 53))
    check("absent port not matched", not ovh._rules_accept_port(SERVER_RULES, "tcp", 5662))
    check("protocol not confused", not ovh._rules_accept_port(SERVER_RULES, "udp", 443))


def test_warnings() -> None:
    audit = ovh.EgressAudit(reject_targets={6: "icmp6-port-unreachable"}, root_exempt={6: True})
    audit.findings = [ovh.EgressFinding("tcp", 5662, 6, "test rig", False, "not listed")]
    ovh._add_egress_warnings(audit)
    joined = " ".join(audit.warnings)
    check(
        "REJECT/ECONNREFUSED ambiguity explained",
        "ECONNREFUSED" in joined and "server_egress_blocked" in joined,
    )
    check("root exemption warned about", "uid 0" in joined)

    # csf.conf allows the port but no live rule does: the edit was never applied.
    stale = ovh.EgressAudit(policies={6: "DROP"}, rules={6: SERVER_RULES})
    stale.findings = [ovh.EgressFinding("tcp", 5662, 6, "x", True, "TCP6_OUT covers this port")]
    ovh._add_egress_warnings(stale)
    check("unapplied csf edit detected", any("csf -r" in w for w in stale.warnings))

    applied = ovh.EgressAudit(policies={6: "DROP"}, rules={6: SERVER_RULES})
    applied.findings = [ovh.EgressFinding("tcp", 443, 6, "x", True, "TCP6_OUT covers this port")]
    ovh._add_egress_warnings(applied)
    check("no false stale warning", not any("csf -r" in w for w in applied.warnings))


def test_ipv6_disabled() -> None:
    """With IPV6 = "0" CSF writes no ip6tables rules, so its *6_OUT lists must not be trusted."""
    audit = ovh.EgressAudit(policies={6: "DROP"}, rules={6: SERVER_RULES})
    audit.csf_ipv6 = False
    finding = ovh._audit_one_port(audit, {"TCP6_OUT": "1:65535"}, "tcp", 5662, 6, "x")
    check(
        "IPV6=0 falls through to the live chain",
        finding.allowed is False and "policy is DROP" in finding.detail,
    )

    v4 = ovh.EgressAudit(policies={4: "ACCEPT"})
    v4.csf_ipv6 = False
    finding = ovh._audit_one_port(v4, {"TCP_OUT": "1:65535"}, "tcp", 5662, 4, "x")
    check(
        "IPV6=0 leaves the IPv4 lists authoritative",
        finding.allowed is True and "TCP_OUT" in finding.detail,
    )


def test_unknown_is_not_a_pass() -> None:
    finding = ovh._audit_one_port(ovh.EgressAudit(), {"TCP_OUT": "80"}, "udp", 4672, 4, "x")
    check(
        "missing csf key reports UNKNOWN, never allowed",
        finding.allowed is None and finding.status == "UNKNOWN",
    )

    audit = ovh.EgressAudit()
    audit.findings = [finding]
    check("undetermined findings are not 'clean'", not audit.clean and not audit.blocked)


def test_fix_lines() -> None:
    audit = ovh.EgressAudit()
    audit.findings = [
        ovh.EgressFinding("tcp", 5662, 4, "x", False, ""),
        ovh.EgressFinding("tcp", 5662, 6, "x", False, ""),
        ovh.EgressFinding("tcp", 4662, 4, "x", False, ""),
        ovh.EgressFinding("udp", 5672, 6, "x", False, ""),
    ]
    fixes = ovh._csf_fix_lines(audit)
    check("blocked ports grouped per csf key", fixes.get("TCP_OUT") == "5662,4662")
    check("families kept separate", fixes.get("TCP6_OUT") == "5662")
    check("protocols kept separate", fixes.get("UDP6_OUT") == "5672")


def main() -> int:
    print("=== ovh-ipv6-config egress audit tests ===\n")
    test_port_lists()
    test_rule_heuristic()
    test_warnings()
    test_ipv6_disabled()
    test_unknown_is_not_a_pass()
    test_fix_lines()
    print(f"\n--- Passed: {_passed}, Failed: {_failed} ---")
    return 1 if _failed else 0


if __name__ == "__main__":
    sys.exit(main())
