#import "macosmediacenter.h"
#include "artworkutils.h"
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>
#include <QByteArray>
#include <QMetaObject>
#include <QPointer>
#include <thread>

MacOSMediaCenter::MacOSMediaCenter(QObject *parent)
    : ISystemMediaInterface(parent) {
  connectRemoteCommands();
  // Initial now-playing snapshot.
  state_.title.clear();
  state_.artist.clear();
  state_.durationMs = 0;
  state_.positionMs = 0;
  state_.playbackState = PlaybackState::Paused;
  state_.artworkData.clear();
  clearCachedArtwork();
  pushNowPlayingToSystem();
}

MacOSMediaCenter::~MacOSMediaCenter() {
  disconnectRemoteCommands();
  clearCachedArtwork();
}

void MacOSMediaCenter::connectRemoteCommands() {
  MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];

  // Play
  playCommandTarget_ =
      [cc.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                          MPRemoteCommandEvent *event) {
        emit playRequested();
        return MPRemoteCommandHandlerStatusSuccess;
      }];

  // Pause
  pauseCommandTarget_ =
      [cc.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                           MPRemoteCommandEvent *event) {
        emit pauseRequested();
        return MPRemoteCommandHandlerStatusSuccess;
      }];

  // Toggle
  toggleCommandTarget_ = [cc.togglePlayPauseCommand
      addTargetWithHandler:^MPRemoteCommandHandlerStatus(
          MPRemoteCommandEvent *event) {
        emit toggleRequested();
        return MPRemoteCommandHandlerStatusSuccess;
      }];

  // Next
  nextCommandTarget_ =
      [cc.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                               MPRemoteCommandEvent *event) {
        emit nextRequested();
        return MPRemoteCommandHandlerStatusSuccess;
      }];

  // Previous
  previousCommandTarget_ = [cc.previousTrackCommand
      addTargetWithHandler:^MPRemoteCommandHandlerStatus(
          MPRemoteCommandEvent *event) {
        emit previousRequested();
        return MPRemoteCommandHandlerStatusSuccess;
      }];

  // Seek
  cc.changePlaybackPositionCommand.enabled = YES;
  seekCommandTarget_ = [cc.changePlaybackPositionCommand
      addTargetWithHandler:^MPRemoteCommandHandlerStatus(
          MPRemoteCommandEvent *event) {
        MPChangePlaybackPositionCommandEvent *positionEvent =
            (MPChangePlaybackPositionCommandEvent *)event;
        const qint64 requestedMs = positionEvent.positionTime * 1000.0;
        emit seekRequested(requestedMs);
        return MPRemoteCommandHandlerStatusSuccess;
      }];
}

void MacOSMediaCenter::disconnectRemoteCommands() {
  MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];

  if (playCommandTarget_) {
    [cc.playCommand removeTarget:playCommandTarget_];
    playCommandTarget_ = nil;
  }
  if (pauseCommandTarget_) {
    [cc.pauseCommand removeTarget:pauseCommandTarget_];
    pauseCommandTarget_ = nil;
  }
  if (toggleCommandTarget_) {
    [cc.togglePlayPauseCommand removeTarget:toggleCommandTarget_];
    toggleCommandTarget_ = nil;
  }
  if (nextCommandTarget_) {
    [cc.nextTrackCommand removeTarget:nextCommandTarget_];
    nextCommandTarget_ = nil;
  }
  if (previousCommandTarget_) {
    [cc.previousTrackCommand removeTarget:previousCommandTarget_];
    previousCommandTarget_ = nil;
  }
  if (seekCommandTarget_) {
    [cc.changePlaybackPositionCommand removeTarget:seekCommandTarget_];
    seekCommandTarget_ = nil;
  }
}

void MacOSMediaCenter::setTitleAndArtist(const QString &title,
                                         const QString &artist) {
  ++artworkTaskId_;
  state_.title = title;
  state_.artist = artist;
  state_.playbackState = PlaybackState::Playing;
  state_.positionMs = 0;
  state_.artworkData.clear();
  clearCachedArtwork();

  pushNowPlayingToSystem();
}

void MacOSMediaCenter::setDuration(qint64 durationMs) {
  state_.durationMs = durationMs;
  pushNowPlayingToSystem();
}

void MacOSMediaCenter::setPlaybackState(PlaybackState state) {
  state_.playbackState = state;
  if (state == PlaybackState::Stopped) {
    state_.positionMs = 0;
  }
  pushNowPlayingToSystem();
}

void MacOSMediaCenter::setPosition(qint64 positionMs) {
  state_.positionMs = positionMs;
  pushNowPlayingToSystem();
}

void MacOSMediaCenter::setArtwork(const QByteArray &imageData) {
  const uint64_t taskId = ++artworkTaskId_;

  if (imageData.isEmpty()) {
    if (state_.artworkData.isEmpty()) {
      return;
    }
    state_.artworkData.clear();
    clearCachedArtwork();
    pushNowPlayingToSystem();
    return;
  }

  QPointer<MacOSMediaCenter> self(this);
  const QByteArray imageDataCopy = imageData;
  std::thread([self, taskId, imageDataCopy]() {
    const QByteArray preparedArtworkPng =
        ArtworkUtils::normalizeArtworkToPng(imageDataCopy, 512);
    if (!self) {
      return;
    }

    QMetaObject::invokeMethod(
        self,
        [self, taskId, preparedArtworkPng]() {
          if (!self) {
            return;
          }
          if (taskId != self->artworkTaskId_.load(std::memory_order_relaxed)) {
            return;
          }
          if (preparedArtworkPng.isEmpty() ||
              self->state_.artworkData == preparedArtworkPng) {
            return;
          }

          self->state_.artworkData = preparedArtworkPng;
          self->clearCachedArtwork();
          self->pushNowPlayingToSystem();
        },
        Qt::QueuedConnection);
  }).detach();
}

void MacOSMediaCenter::clearCachedArtwork() { cachedArtwork_ = nil; }

void MacOSMediaCenter::pushNowPlayingToSystem() {
  NSMutableDictionary *info = [NSMutableDictionary dictionary];

  if (!state_.title.isEmpty()) {
    info[MPMediaItemPropertyTitle] = state_.title.toNSString();
  }
  if (!state_.artist.isEmpty()) {
    info[MPMediaItemPropertyArtist] = state_.artist.toNSString();
  }
  info[MPMediaItemPropertyPlaybackDuration] = @(state_.durationMs / 1000.0);
  info[MPNowPlayingInfoPropertyElapsedPlaybackTime] =
      @(state_.positionMs / 1000.0);
  info[MPNowPlayingInfoPropertyPlaybackRate] =
      (state_.playbackState == PlaybackState::Playing ? @1 : @0);
  if (!state_.artworkData.isEmpty()) {
    MPMediaItemArtwork *artwork = cachedArtwork_;
    if (artwork == nil) {
      NSData *artData = [NSData dataWithBytes:state_.artworkData.constData()
                                       length:state_.artworkData.size()];
      NSImage *baseImage = [[NSImage alloc] initWithData:artData];
      if (baseImage != nil) {
        artwork = [[MPMediaItemArtwork alloc]
            initWithBoundsSize:baseImage.size
                requestHandler:^NSImage *_Nonnull(CGSize size) {
                  Q_UNUSED(size);
                  return baseImage;
                }];
        if (artwork != nil) {
          cachedArtwork_ = artwork;
        }
      }
    }

    if (artwork != nil) {
      info[MPMediaItemPropertyArtwork] = artwork;
    }
  }

  [MPNowPlayingInfoCenter.defaultCenter setNowPlayingInfo:info];
}
