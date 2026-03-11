# Integration test fixtures

Fixture layout:

- `geometry/encode/*.csv`: input WKT + precision, output WKP string.
- `geometry/decode/*.csv`: input WKP string, output WKT.
- `floats/encode/*.csv`: input float matrix + precisions, output encoded polyline string.
- `floats/decode/*.csv`: input encoded string + precisions, output float matrix.

CSV format is always exactly two rows:

1. `input,<json-string-or-text>`
2. `output,<expected-value-or-TODO>`

If `output` is `TODO`, run Python tests with `WKP_UPDATE_INTEGRATION_FIXTURES=1` to auto-fill with actual outputs.
