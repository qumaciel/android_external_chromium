// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/theme_change_processor.h"

#include "base/logging.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/engine/syncapi.h"
#include "chrome/browser/sync/glue/theme_util.h"
#include "chrome/browser/sync/protocol/theme_specifics.pb.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/common/extensions/extension.h"
#include "content/common/notification_details.h"
#include "content/common/notification_source.h"

namespace browser_sync {

ThemeChangeProcessor::ThemeChangeProcessor(
    UnrecoverableErrorHandler* error_handler)
    : ChangeProcessor(error_handler),
      profile_(NULL) {
  DCHECK(error_handler);
}

ThemeChangeProcessor::~ThemeChangeProcessor() {}

void ThemeChangeProcessor::Observe(NotificationType type,
                                   const NotificationSource& source,
                                   const NotificationDetails& details) {
  DCHECK(running());
  DCHECK(profile_);
  DCHECK(type == NotificationType::BROWSER_THEME_CHANGED);

  sync_api::WriteTransaction trans(share_handle());
  sync_api::WriteNode node(&trans);
  if (!node.InitByClientTagLookup(syncable::THEMES,
                                  kCurrentThemeClientTag)) {
    std::string err = "Could not create node with client tag: ";
    error_handler()->OnUnrecoverableError(FROM_HERE,
                                          err + kCurrentThemeClientTag);
    return;
  }

  sync_pb::ThemeSpecifics old_theme_specifics = node.GetThemeSpecifics();
  // Make sure to base new_theme_specifics on old_theme_specifics so
  // we preserve the state of use_system_theme_by_default.
  sync_pb::ThemeSpecifics new_theme_specifics = old_theme_specifics;
  GetThemeSpecificsFromCurrentTheme(profile_, &new_theme_specifics);
  // Do a write only if something actually changed so as to guard
  // against cycles.
  if (!AreThemeSpecificsEqual(old_theme_specifics, new_theme_specifics)) {
    node.SetThemeSpecifics(new_theme_specifics);
  }
  return;
}

void ThemeChangeProcessor::ApplyChangesFromSyncModel(
    const sync_api::BaseTransaction* trans,
    const sync_api::SyncManager::ChangeRecord* changes,
    int change_count) {
  if (!running()) {
    return;
  }
  // TODO(akalin): Normally, we should only have a single change and
  // it should be an update.  However, the syncapi may occasionally
  // generates multiple changes.  When we fix syncapi to not do that,
  // we can remove the extra logic below.  See:
  // http://code.google.com/p/chromium/issues/detail?id=41696 .
  if (change_count < 1) {
    std::string err("Unexpected change_count: ");
    err += change_count;
    error_handler()->OnUnrecoverableError(FROM_HERE, err);
    return;
  }
  if (change_count > 1) {
    LOG(WARNING) << change_count << " theme changes detected; "
                 << "only applying the last one";
  }
  const sync_api::SyncManager::ChangeRecord& change =
      changes[change_count - 1];
  if (change.action != sync_api::SyncManager::ChangeRecord::ACTION_UPDATE &&
      change.action != sync_api::SyncManager::ChangeRecord::ACTION_DELETE) {
    std::string err = "strange theme change.action " + change.action;
    error_handler()->OnUnrecoverableError(FROM_HERE, err);
    return;
  }
  sync_pb::ThemeSpecifics theme_specifics;
  // If the action is a delete, simply use the default values for
  // ThemeSpecifics, which would cause the default theme to be set.
  if (change.action != sync_api::SyncManager::ChangeRecord::ACTION_DELETE) {
    sync_api::ReadNode node(trans);
    if (!node.InitByIdLookup(change.id)) {
      error_handler()->OnUnrecoverableError(FROM_HERE,
                                            "Theme node lookup failed.");
      return;
    }
    DCHECK_EQ(node.GetModelType(), syncable::THEMES);
    DCHECK(profile_);
    theme_specifics = node.GetThemeSpecifics();
  }
  StopObserving();
  SetCurrentThemeFromThemeSpecificsIfNecessary(theme_specifics, profile_);
  StartObserving();
}

void ThemeChangeProcessor::StartImpl(Profile* profile) {
  DCHECK(profile);
  profile_ = profile;
  StartObserving();
}

void ThemeChangeProcessor::StopImpl() {
  StopObserving();
  profile_ = NULL;
}

void ThemeChangeProcessor::StartObserving() {
  DCHECK(profile_);
  VLOG(1) << "Observing BROWSER_THEME_CHANGED";
  notification_registrar_.Add(
      this, NotificationType::BROWSER_THEME_CHANGED,
      Source<ThemeService>(
          ThemeServiceFactory::GetForProfile(profile_)));
}

void ThemeChangeProcessor::StopObserving() {
  DCHECK(profile_);
  VLOG(1) << "Unobserving all notifications";
  notification_registrar_.RemoveAll();
}

}  // namespace browser_sync
