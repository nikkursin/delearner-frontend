# Internal database import/export checks

File transfer is retained for local diagnostics, migration checks and recovery
verification. Production settings do not expose it. Normal multi-device sync
uses authentication, the transactional outbox, pull and bootstrap.

The callable entry points on `DLAppStateManager` are:

- `exportDatabase(targetPath)`: copy the local SQLite database to an explicit
  path, including a `.devocab` path; creates the destination directory.
- `importDatabaseReplace(sourcePath)`: replace and reopen the local database.
- `importDatabaseMerge(sourcePath)`: merge vocabulary, groups, forms and review
  statistics into the current database, assigning new vocabulary/group UUIDs.

Plain paths and local file URLs are accepted. Use isolated temporary databases
and an inactive network connection for these internal operations. Replacement
copies local sync bookkeeping as well as domain data; merge is a local vocabulary
utility and does not enqueue imported data for upload. Neither operation is an
account-transfer or multi-device synchronization API. The automated round trips
use the current SQLite schema and do not promise arbitrary legacy-file support.

`TestAppStateAuthBootstrap` exercises these actual entry points without QML,
Felgo or an OS share sheet:

- `internalExportReplaceRestoresDatabaseState`: exports an open database,
  restores it after mutation, and compares every table before and after reopen,
  including UUIDs, relationships, review statistics and pending outbox events.
- `internalExportMergeRetainsVocabularyRelationships`: merges into a database
  containing local data and checks new identities, membership, noun forms,
  review counts and foreign-key integrity.
- `internalImportRejectsMissingAndSameDatabaseWithoutChanges`: rejects empty,
  missing and same-database import paths while preserving existing data.

Run the frontend suite from the root with `scripts/test-all.sh frontend`.
`TestProductionSettingsUi` checks that file-transfer actions stay absent from
production settings. Run `tests/domain_relationship_tombstone_matrix.sh` for
production frontend/backend two-device propagation and fresh bootstrap without
calling any of these manual file APIs. Its backend uses the deterministic SQLite
test adapter described in the root matrix README.
