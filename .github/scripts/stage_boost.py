"""Expose only architecture-neutral Boost headers to cross compilers."""
import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    version = re.search(r"#define BOOST_VERSION (\d+)", (source / "version.hpp").read_text())[1]
    destination = args.destination.resolve()
    include = destination / "include"
    include.mkdir(parents=True, exist_ok=True)
    link = include / "boost"
    if link.exists() or link.is_symlink():
        if link.resolve() != source:
            raise RuntimeError("Boost staging directory belongs to another source")
    else:
        link.symlink_to(source, target_is_directory=True)
    (destination / "BoostConfig.cmake").write_text(
        f'set(Boost_FOUND TRUE)\nset(Boost_VERSION {version})\n'
        f'set(Boost_INCLUDE_DIRS "{include.as_posix()}")\n'
        'if(NOT TARGET Boost::headers)\n'
        '  add_library(Boost::headers INTERFACE IMPORTED)\n'
        f'  set_target_properties(Boost::headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "{include.as_posix()}")\n'
        'endif()\n', encoding="utf-8")


if __name__ == "__main__":
    main()
