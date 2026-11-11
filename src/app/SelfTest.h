#pragma once

namespace launcher {

/// Two modes of the launcher program that open no window and need no network. They let
/// the packaging scripts check a finished release package from the outside
/// (docs/03-entscheidungen.md, D-46).
///
///   KERJAMI_Launcher.exe --self-test <result.json>
///       Checks that this very program is complete: TLS for HTTPS is available, the project
///       list built into the program is valid, hashing and decompression work, and the
///       folder the program lies in is a usable portable folder. Writes the result as JSON.
///       Exit code 0: everything is fine, 1: a check failed, 2: bad arguments or the result
///       file could not be written.
///
///   KERJAMI_Launcher.exe --check-package --zip <file> --sha256 <hash> --version <version>
///                        --work <folder> --out <result.json>
///       Prepares the release ZIP exactly as the launcher's self-update would: hash, the
///       rules for archives, extraction, manifest, the hash of every file. Writes the
///       result as JSON. Same exit codes.
///
/// A program that is started with one of these flags does nothing else.
class SelfTest {
public:
    /// True if the command line asks for one of the modes. Looks at the raw arguments, so
    /// main() can decide before any application object exists.
    static bool isRequested(int argc, char* argv[]);

    /// Runs the requested mode and returns the exit code. Creates its own QCoreApplication.
    static int runProgram(int argc, char* argv[]);
};

} // namespace launcher
