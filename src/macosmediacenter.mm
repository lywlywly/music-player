#import "macosmediacenter.h"
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

MacOSMediaCenter::MacOSMediaCenter(QObject *parent)
    : ISystemMediaInterface(parent) {
  connectRemoteCommands();
  // show in media center and set status to pause
  setTitleAndArtist("", "");
  setDuration(0);
  setPlaybackState(PlaybackState::Paused);
}

void MacOSMediaCenter::connectRemoteCommands() {
  MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];

  // Play
  [cc.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                      MPRemoteCommandEvent *event) {
    emit playRequested();
    return MPRemoteCommandHandlerStatusSuccess;
  }];

  // Pause
  [cc.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                       MPRemoteCommandEvent *event) {
    emit pauseRequested();
    return MPRemoteCommandHandlerStatusSuccess;
  }];

  // Toggle
  [cc.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                                 MPRemoteCommandEvent *event) {
    emit toggleRequested();
    return MPRemoteCommandHandlerStatusSuccess;
  }];

  // Next
  [cc.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                           MPRemoteCommandEvent *event) {
    emit nextRequested();
    return MPRemoteCommandHandlerStatusSuccess;
  }];

  // Previous
  [cc.previousTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                               MPRemoteCommandEvent *event) {
    emit previousRequested();
    return MPRemoteCommandHandlerStatusSuccess;
  }];

  // Seek
  cc.changePlaybackPositionCommand.enabled = YES;
  [cc.changePlaybackPositionCommand
      addTargetWithHandler:^MPRemoteCommandHandlerStatus(
          MPRemoteCommandEvent *event) {
        MPChangePlaybackPositionCommandEvent *positionEvent =
            (MPChangePlaybackPositionCommandEvent *)event;
        const qint64 requestedMs = positionEvent.positionTime * 1000.0;
        emit seekRequested(requestedMs);
        return MPRemoteCommandHandlerStatusSuccess;
      }];
}

void MacOSMediaCenter::setTitleAndArtist(const QString &title,
                                         const QString &artist) {
  state_.title = title;
  state_.artist = artist;
  state_.playbackState = PlaybackState::Playing;
  state_.positionMs = 0;

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

  [MPNowPlayingInfoCenter.defaultCenter setNowPlayingInfo:info];
}
