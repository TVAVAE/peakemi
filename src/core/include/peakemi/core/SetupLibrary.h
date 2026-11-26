#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/TestSetup.h>

#include <QString>

#include <string>
#include <string_view>
#include <vector>

namespace peakemi {

/// Versioned JSON container for one test set-up, ".peakemi-setup.json".
class TestSetupSerializer
{
public:
    static constexpr int CurrentSchemaVersion = 1;
    static constexpr int MinimumSupportedSchemaVersion = 1;

    [[nodiscard]] static std::string toJson(const TestSetup& setup, bool pretty = true);
    [[nodiscard]] static Result<TestSetup> fromJson(std::string_view text);

    [[nodiscard]] static Status save(const TestSetup& setup, const QString& path);
    [[nodiscard]] static Result<TestSetup> load(const QString& path);
};

/// The saved set-ups: one file each in a folder, which can be a shared folder
/// so a lab works from one list of benches.
class SetupLibrary
{
public:
    static constexpr std::string_view FileSuffix = ".peakemi-setup.json";

    explicit SetupLibrary(QString directory);

    /// The application's own folder for set-ups.
    [[nodiscard]] static QString defaultDirectory();

    [[nodiscard]] const QString& directory() const { return m_directory; }

    struct Contents
    {
        std::vector<TestSetup> setups; ///< sorted by name
        /// Files that could not be read, with the reason. A broken file does
        /// not hide the others.
        std::vector<std::string> problems;
    };

    [[nodiscard]] Contents load() const;
    /// Writes the set-up to its own file, creating the folder when needed.
    [[nodiscard]] Status save(const TestSetup& setup) const;
    [[nodiscard]] Status remove(std::string_view id) const;
    [[nodiscard]] QString pathFor(std::string_view id) const;

private:
    QString m_directory;
};

} // namespace peakemi
