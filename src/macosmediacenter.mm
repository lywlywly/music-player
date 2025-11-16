#import "macosmediacenter.h"
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

MacOSMediaCenter::MacOSMediaCenter(QObject *parent)
    : ISystemMediaInterface(parent) {
  connectRemoteCommands();
  // show in media center and set status to pause
  updateNowPlaying("", "", 0, 0, true);
  updateNowPlaying("", "", 0, 0, false);
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
}

void MacOSMediaCenter::updateNowPlaying(const QString &title,
                                        const QString &artist,
                                        qint64 durationSec, qint64 positionSec,
                                        bool isPlaying) {
  NSMutableDictionary *info = [NSMutableDictionary dictionary];

  if (!title.isEmpty())
    info[MPMediaItemPropertyTitle] = title.toNSString();
  if (!artist.isEmpty())
    info[MPMediaItemPropertyArtist] = artist.toNSString();

  info[MPMediaItemPropertyPlaybackDuration] = @(durationSec);
  info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(positionSec);
  info[MPNowPlayingInfoPropertyPlaybackRate] = (isPlaying ? @1 : @0);

  [MPNowPlayingInfoCenter.defaultCenter setNowPlayingInfo:info];
}

void MacOSMediaCenter::setTrackInfo(const QString &title, const QString &artist,
                                    qint64 durationMs) {
  state_.title = title;
  state_.artist = artist;
  state_.durationMs = durationMs;
  state_.isPlaying = true;

  pushNowPlayingToSystem();
}

void MacOSMediaCenter::updatePosition(qint64 positionMs) {
  state_.positionMs = positionMs;
  pushNowPlayingToSystem();
}

void MacOSMediaCenter::updatePlaybackState(bool isPlaying) {
  state_.isPlaying = isPlaying;
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
  info[MPNowPlayingInfoPropertyPlaybackRate] = (state_.isPlaying ? @1 : @0);

  [MPNowPlayingInfoCenter.defaultCenter setNowPlayingInfo:info];
}
