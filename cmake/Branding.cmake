# Central branding and distribution configuration.
#
# Every user-visible product name, the executable names, the GitHub coordinates
# and the registry URL come from this file. To rebrand the launcher, change the
# values here and nowhere else. Identifiers in the source code are brand-neutral.

set(BRAND_PRODUCT_NAME        "KERJAMI")
set(BRAND_DISPLAY_NAME        "KERJAMI Launcher")

# File name without extension. The launcher is a single program: it also does the
# update of itself (docs/03-entscheidungen.md, D-40).
set(BRAND_EXECUTABLE_NAME     "KERJAMI_Launcher")

# GitHub repository that publishes the launcher's own releases.
set(BRAND_GITHUB_OWNER        "KERSEX")
set(BRAND_LAUNCHER_REPOSITORY "KERJAMI_Launcher")

# Name of the portable ZIP that carries a launcher release. "*" stands for the
# version. The file is both the download for new users and the package the
# self-update installs (docs/03-entscheidungen.md, D-39).
set(BRAND_UPDATE_ASSET_PATTERN "${BRAND_PRODUCT_NAME}-*-Windows-x64-Portable.zip")

