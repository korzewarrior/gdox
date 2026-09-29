"""Private NBD module ownership checks."""

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
