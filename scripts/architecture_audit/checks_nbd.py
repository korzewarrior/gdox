"""Private NBD module ownership and size checks."""

from __future__ import annotations

from .repository import Repository

NBD_MODULES = (
    "nbd_protocol.c",
    "nbd_socket.c",
    "nbd_tcp.c",
    "nbd_telemetry.c",
    "nbd_token.c",
    "nbd_wire.c",
)


def _source_name(value: str) -> str:
    return value.rsplit("/", 1)[-1]


def check_nbd(repository: Repository) -> list[str]:
    failures: list[str] = []
    configured = tuple(
        _source_name(source)
        for source in repository.cmake.variable("GDOX_NBD_SOURCES")
    )
    if any(module not in configured for module in NBD_MODULES):
        failures.append("platform graph omits a focused private NBD module")
    elif any(configured.count(module) != 1 for module in NBD_MODULES):
        failures.append("platform graph duplicates a private NBD implementation")
    elif set(configured) != set(NBD_MODULES):
        failures.append("platform graph mixes unrelated code into private NBD modules")

    modules = {
        name: repository.source(f"src/platform/{name}") for name in NBD_MODULES
    }
    for name, limit in (
        ("nbd_tcp.c", 500),
        ("nbd_protocol.c", 650),
        ("nbd_socket.c", 500),
        ("nbd_wire.c", 250),
        ("nbd_telemetry.c", 175),
        ("nbd_token.c", 150),
    ):
        if modules[name].line_count > limit:
            failures.append(f"{name} exceeds its focused module boundary")

    for name, limit in (
        ("nbd_internal.h", 60),
        ("nbd_protocol.h", 30),
        ("nbd_socket.h", 80),
        ("nbd_telemetry.h", 50),
        ("nbd_token.h", 30),
        ("nbd_wire.h", 125),
    ):
        if repository.source(f"src/platform/{name}").line_count > limit:
            failures.append(f"{name} exceeds its private contract boundary")

    for forbidden in (
        "NBD_INIT_MAGIC",
        "recv(",
        "send(",
        "setsockopt(",
        "BCryptGenRandom(",
        "getrandom(",
    ):
        if forbidden in modules["nbd_tcp.c"].text:
            failures.append(
                f"NBD lifecycle owns moved implementation detail {forbidden}"
            )
    for forbidden in (
        '"gdox/nbd.h"',
        '"platform/nbd_internal.h"',
        '"platform/nbd_socket.h"',
        "malloc(",
        "gdox_disc_",
    ):
        if forbidden in modules["nbd_wire.c"].text:
            failures.append(
                f"NBD wire codec crosses its pure boundary through {forbidden}"
            )
    for forbidden in ("NBD_INIT_MAGIC", "gdox_nbd_export", "gdox_disc_"):
        if forbidden in modules["nbd_socket.c"].text:
            failures.append(
                f"NBD socket adapter owns protocol detail {forbidden}"
            )
    return failures
