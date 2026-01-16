(function() {
    let fadeTimeout;

    function fade(instance, startingVolume) {
        instance._isFadingOut = true;
        const newVolume = Math.max(0, startingVolume - 15);
        window.api.player.setVolume(newVolume);

        if (newVolume <= 0) {
            instance._isFadingOut = false;
            return Promise.resolve();
        }

        return new Promise((resolve, reject) => {
            cancelFadeTimeout();
            fadeTimeout = setTimeout(() => {
                fade(instance, newVolume).then(resolve, reject);
            }, 100);
        });
    }

    function cancelFadeTimeout() {
        if (fadeTimeout) {
            clearTimeout(fadeTimeout);
            fadeTimeout = null;
        }
    }

    class mpvAudioPlayer {
        constructor({ events, appHost, appSettings }) {
            this.events = events;
            this.appHost = appHost;
            this.appSettings = appSettings;

            this.name = 'MPV Audio Player';
            this.type = 'mediaplayer';
            this.id = 'mpvaudioplayer';
            this.syncPlayWrapAs = 'htmlaudioplayer';
            this.useServerPlaybackInfoForAudio = true;

            this._duration = undefined;
            this._currentTime = null;
            this._paused = false;
            this._volume = this.getSavedVolume() * 100;
            this._playRate = 1;
            this._hasConnection = false;
            this._muted = false;
            this._timeUpdateTimer = null;
            this._lastTimerTick = null;

            this.startTimeUpdateTimer = () => {
                if (this._timeUpdateTimer) return;
                this._lastTimerTick = Date.now();
                this._timeUpdateTimer = setInterval(() => {
                    if (this._paused || this._currentTime === null) return;
                    const now = Date.now();
                    const elapsed = now - this._lastTimerTick;
                    this._lastTimerTick = now;
                    const rate = this.getPlaybackRate() || 1.0;
                    this._currentTime += elapsed * rate;
                    this.events.trigger(this, 'timeupdate');
                }, 250);
            };

            this.stopTimeUpdateTimer = () => {
                if (this._timeUpdateTimer) {
                    clearInterval(this._timeUpdateTimer);
                    this._timeUpdateTimer = null;
                }
            };

            this.onPlaying = () => {
                if (!this._started) {
                    this._started = true;
                    const volume = this.getSavedVolume() * 100;
                    this.setVolume(volume, volume !== this._volume);
                }
                this.setPlaybackRate(this.getPlaybackRate());
                if (this._paused) {
                    this._paused = false;
                    this.events.trigger(this, 'unpause');
                }
                this.startTimeUpdateTimer();
                this.events.trigger(this, 'playing');
            };

            this.onTimeUpdate = (time) => {
                if (!this._isFadingOut) {
                    this._currentTime = time;
                    this._lastTimerTick = Date.now();
                    this.events.trigger(this, 'timeupdate');
                }
            };

            this.onEnded = () => {
                this.onEndedInternal();
            };

            this.onPause = () => {
                this._paused = true;
                this.stopTimeUpdateTimer();
                this.events.trigger(this, 'pause');
            };

            this.onDuration = (duration) => {
                this._duration = duration;
            };

            this.onError = (error) => {
                console.error('[Media] [Audio] media error:', error);
                this.events.trigger(this, 'error', [{ type: 'mediadecodeerror' }]);
            };
        }

        play(options) {
            this._started = false;
            this._currentTime = null;
            this._duration = undefined;

            const player = window.api.player;
            if (!this._hasConnection) {
                this._hasConnection = true;
                player.playing.connect(this.onPlaying);
                player.positionUpdate.connect(this.onTimeUpdate);
                player.finished.connect(this.onEnded);
                player.updateDuration.connect(this.onDuration);
                player.error.connect(this.onError);
                player.paused.connect(this.onPause);
            }

            return this.setCurrentSrc(options);
        }

        setCurrentSrc(options) {
            return new Promise((resolve) => {
                const val = options.url;
                this._currentSrc = val;
                console.log('[Media] [Audio] Playing:', val);

                const ms = (options.playerStartPositionTicks || 0) / 10000;
                this._currentPlayOptions = options;
                this._currentTime = ms;

                window.api.player.load(val,
                    { startMilliseconds: ms, autoplay: true },
                    { type: 'music', metadata: options.item },
                    -1,
                    -1,
                    resolve);
            });
        }

        onEndedInternal() {
            this.stopTimeUpdateTimer();
            this.events.trigger(this, 'stopped', [{ src: this._currentSrc }]);
            this._currentTime = null;
            this._currentSrc = null;
            this._currentPlayOptions = null;
        }

        stop(destroyPlayer) {
            cancelFadeTimeout();
            const src = this._currentSrc;

            if (src) {
                if (!destroyPlayer) {
                    this.pause();
                    this.onEndedInternal();
                    return Promise.resolve();
                }

                const originalVolume = this._volume;
                return fade(this, this._volume).then(() => {
                    this.pause();
                    this.setVolume(originalVolume, false);
                    this.onEndedInternal();
                    this.destroy();
                });
            }
            return Promise.resolve();
        }

        destroy() {
            this.stopTimeUpdateTimer();
            window.api.player.stop();
            const player = window.api.player;
            this._hasConnection = false;
            player.playing.disconnect(this.onPlaying);
            player.positionUpdate.disconnect(this.onTimeUpdate);
            player.finished.disconnect(this.onEnded);
            player.updateDuration.disconnect(this.onDuration);
            player.error.disconnect(this.onError);
            player.paused.disconnect(this.onPause);
            this._duration = undefined;
        }

        getSavedVolume() {
            return this.appSettings ? this.appSettings.get('volume') || 1 : 1;
        }

        currentSrc() {
            return this._currentSrc;
        }

        canPlayMediaType(mediaType) {
            return (mediaType || '').toLowerCase() === 'audio';
        }

        getDeviceProfile(item, options) {
            if (this.appHost && this.appHost.getDeviceProfile) {
                return this.appHost.getDeviceProfile(item, options);
            }
            return Promise.resolve({});
        }

        currentTime(val) {
            if (val != null) {
                this._currentTime = val;
                this._lastTimerTick = Date.now();
                window.api.player.seekTo(val);
                return;
            }
            return this._currentTime;
        }

        currentTimeAsync() {
            return new Promise((resolve) => {
                window.api.player.getPosition(resolve);
            });
        }

        duration() {
            return this._duration || null;
        }

        seekable() {
            return Boolean(this._duration);
        }

        getBufferedRanges() {
            return [];
        }

        pause() {
            window.api.player.pause();
        }

        resume() {
            this._paused = false;
            window.api.player.play();
        }

        unpause() {
            window.api.player.play();
        }

        paused() {
            return this._paused;
        }

        setPlaybackRate(value) {
            this._playRate = value;
            window.api.player.setPlaybackRate(value * 1000);
        }

        getPlaybackRate() {
            return this._playRate;
        }

        getSupportedPlaybackRates() {
            return [0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0].map(id => ({ name: id + 'x', id }));
        }

        saveVolume(value) {
            if (value && this.appSettings) {
                this.appSettings.set('volume', value);
            }
        }

        setVolume(val, save = true) {
            this._volume = val;
            if (save) {
                this.saveVolume((val || 100) / 100);
                this.events.trigger(this, 'volumechange');
            }
            window.api.player.setVolume(val);
        }

        getVolume() {
            return this._volume;
        }

        volumeUp() {
            this.setVolume(Math.min(this.getVolume() + 2, 100));
        }

        volumeDown() {
            this.setVolume(Math.max(this.getVolume() - 2, 0));
        }

        setMute(mute, triggerEvent = true) {
            this._muted = mute;
            window.api.player.setMuted(mute);
            if (triggerEvent) {
                this.events.trigger(this, 'volumechange');
            }
        }

        isMuted() {
            return this._muted;
        }

        supports(feature) {
            return ['PlaybackRate'].includes(feature);
        }
    }

    window._mpvAudioPlayer = mpvAudioPlayer;
    console.log('[Media] mpvAudioPlayer plugin installed');
})();
