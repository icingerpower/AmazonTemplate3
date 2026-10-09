# Template title translation

## 2026-10-09 — English title measurements

- `FillerTitle` delegates title-body prompts and validation to
  `fillers/TitleTranslation`. English translations render body measurements as
  `XX in (YY cm)`, e.g. `7 cm` becomes `2.76 in (7 cm)`.
- Translation and selection prompts both receive the original body and locally
  calculated conversions (cm / 2.54, inches rounded to two decimal places).
  Candidate and final/cache validators check numerical conversions and preserve
  the count of cm measurements, including decimal commas, dimensions and ranges.
  The existing two-candidate + selection workflow remains in use.
- The final parentheses contain legal variation information. That suffix stays
  outside the AI prompts; existing color/size assembly in `FillerTitle` is
  unchanged. Splitting now inspects the trailing suffix instead of truncating at
  the first parenthesis, preserving measurement pairs in reused generated titles.
  A final explicit `in (cm)` measurement pair belongs to the body; a standalone
  `(10 cm)` remains a possible size-only variation suffix.
- Title cache keys have a `v2` prefix so old translations do not suppress the
  new instructions. Source and target language equality still follows the
  existing copy path; this change concerns AI title translation to English.
- `FillerTitleTests` covers the supplied French 7 cm heel title, suffix isolation,
  generated-title reuse, conversion validation, old-cache bypass and new-cache
  reuse. Integration tests use temporary XLSX/settings files and an asynchronous
  fake OpenAi2 transport; no real API calls are needed.
