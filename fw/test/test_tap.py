#!/usr/bin/env python3
"""Host-side TAP-state tests for the rpjtag implementation plan."""

from __future__ import annotations

TAP_STATES = [
    "TEST_LOGIC_RESET",
    "RUN_TEST_IDLE",
    "SELECT_DR_SCAN",
    "CAPTURE_DR",
    "SHIFT_DR",
    "EXIT1_DR",
    "PAUSE_DR",
    "EXIT2_DR",
    "UPDATE_DR",
    "SELECT_IR_SCAN",
    "CAPTURE_IR",
    "SHIFT_IR",
    "EXIT1_IR",
    "PAUSE_IR",
    "EXIT2_IR",
    "UPDATE_IR",
]

TAP_GRAPH = {
    "TEST_LOGIC_RESET": {"0": "RUN_TEST_IDLE", "1": "TEST_LOGIC_RESET"},
    "RUN_TEST_IDLE": {"0": "RUN_TEST_IDLE", "1": "SELECT_DR_SCAN"},
    "SELECT_DR_SCAN": {"0": "CAPTURE_DR", "1": "SELECT_IR_SCAN"},
    "CAPTURE_DR": {"0": "SHIFT_DR", "1": "EXIT1_DR"},
    "SHIFT_DR": {"0": "SHIFT_DR", "1": "EXIT1_DR"},
    "EXIT1_DR": {"0": "PAUSE_DR", "1": "UPDATE_DR"},
    "PAUSE_DR": {"0": "PAUSE_DR", "1": "EXIT2_DR"},
    "EXIT2_DR": {"0": "SHIFT_DR", "1": "UPDATE_DR"},
    "UPDATE_DR": {"0": "RUN_TEST_IDLE", "1": "SELECT_DR_SCAN"},
    "SELECT_IR_SCAN": {"0": "CAPTURE_IR", "1": "TEST_LOGIC_RESET"},
    "CAPTURE_IR": {"0": "SHIFT_IR", "1": "EXIT1_IR"},
    "SHIFT_IR": {"0": "SHIFT_IR", "1": "EXIT1_IR"},
    "EXIT1_IR": {"0": "PAUSE_IR", "1": "UPDATE_IR"},
    "PAUSE_IR": {"0": "PAUSE_IR", "1": "EXIT2_IR"},
    "EXIT2_IR": {"0": "SHIFT_IR", "1": "UPDATE_IR"},
    "UPDATE_IR": {"0": "RUN_TEST_IDLE", "1": "SELECT_DR_SCAN"},
}


def shortest_tms_path(start: str, end: str) -> list[int]:
    """Return the shortest TMS bit sequence traversing the real JTAG TAP graph."""
    if start == end:
        return []

    queue: list[tuple[str, list[int]]] = [(start, [])]
    seen = {start}

    while queue:
        state, bits = queue.pop(0)
        for bit, next_state in TAP_GRAPH[state].items():
            new_bits = bits + [int(bit)]
            if next_state == end:
                return new_bits
            if next_state not in seen:
                seen.add(next_state)
                queue.append((next_state, new_bits))

    raise ValueError(f"No path from {start} to {end}")


def test_reset_path() -> None:
    assert shortest_tms_path("TEST_LOGIC_RESET", "RUN_TEST_IDLE") == [0]


def test_shift_path() -> None:
    assert shortest_tms_path("RUN_TEST_IDLE", "SHIFT_DR") == [1, 0, 0]


def test_tap_state_list() -> None:
    assert TAP_STATES[0] == "TEST_LOGIC_RESET"
    assert TAP_STATES[-1] == "UPDATE_IR"


if __name__ == "__main__":
    test_reset_path()
    test_shift_path()
    test_tap_state_list()
    print("tap-state tests passed")
