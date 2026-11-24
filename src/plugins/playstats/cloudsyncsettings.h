#ifndef CLOUDSYNCSETTINGS_H
#define CLOUDSYNCSETTINGS_H

#include <QString>
#include <QUuid>

// The settings keys of cloud sync (docs/cloud-sync.md), shared by the
// coordinator that syncs and the Settings page that sets the UUID.
namespace CloudSyncSettings {
inline constexpr const char *kUserUuid = "playstats/user_uuid";
inline constexpr const char *kDisabledByUser = "playstats/disabled_by_user";
inline constexpr const char *kLastSyncedAt = "playstats/last_synced_at";
inline constexpr const char *kRebasePending = "playstats/rebase_pending";

// Whether uuid, trimmed, is a UUID sync can run with.
inline bool isValidUuid(const QString &uuid) {
  const QString trimmed = uuid.trimmed();
  return !trimmed.isEmpty() && !QUuid(trimmed).isNull();
}
} // namespace CloudSyncSettings

#endif // CLOUDSYNCSETTINGS_H
