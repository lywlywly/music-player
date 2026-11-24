#ifndef ISONGFIELDHOST_H
#define ISONGFIELDHOST_H

#include <QStringList>

// The host's side of ISongFieldProvider, for the plugin that provides it.
class ISongFieldHost {
public:
  virtual ~ISongFieldHost() = default;

  // The values of these keys changed; the host reads them again with
  // ISongFieldProvider::values. Returns at once: the host queues it and runs
  // it on the main thread in a later turn of its event loop, so it may be
  // called inside a listener call and from any thread. Calls still queued
  // when the host is destroyed are dropped.
  virtual void valuesChanged(const QStringList &keys) = 0;
};

#endif // ISONGFIELDHOST_H
