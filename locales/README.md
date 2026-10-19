# Translations

Locale copy lives in `locales/<locale>/strings.json`. The included locales are `zh-Hans`, `zh-Hant`, and `en-US`.

The system language selects Simplified Chinese for Simplified Chinese locales and Traditional Chinese for Traditional Chinese locales. A matching contributed locale is selected automatically; every other locale falls back to English.

## Contribute a language

1. Copy `locales/en-US/strings.json` to a new directory using the locale's BCP 47 code (for example, `locales/fr-FR/strings.json`).
2. Translate the values and keep every key unchanged. Preserve placeholders such as `%llu`, shortcut text, wildcard patterns, and `\n` line breaks.
3. Run `python3 tools/generate_locales.py` to validate key parity and update `src/locales_generated.hpp`. The script discovers locale directories automatically.
4. Submit the JSON file and generated header together in the pull request. Exact system locale matches are selected automatically; any system locale without a matching translation continues to show English.

Do not edit `src/locales_generated.hpp` directly. The JSON files are the translation source of truth.
