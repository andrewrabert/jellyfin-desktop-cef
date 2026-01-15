(function() {
    function getMediaStreamAudioTracks(mediaSource) {
        return mediaSource.MediaStreams.filter(s => s.Type === 'Audio');
    }

    class mpvVideoPlayer {
        constructor({ events, loading, appRouter, globalize, appHost, appSettings, confirm, dashboard }) {
            this.events = events;
            this.loading = loading;
            this.appRouter = appRouter;
            this.globalize = globalize;
            this.appHost = appHost;
            this.appSettings = appSettings;
            if (dashboard && dashboard.default) {
                this.setTransparency = dashboard.default.setBackdropTransparency.bind(dashboard);
            } else {
                this.setTransparency = () => {};
            }

            this.name = 'MPV Video Player';
            this.type = 'mediaplayer';
            this.id = 'mpvvideoplayer';
            this.syncPlayWrapAs = 'htmlvideoplayer';
            this.priority = -1;
            this.useFullSubtitleUrls = true;
            this.isLocalPlayer = true;
            this.isFetching = false;

            this._videoDialog = undefined;
            this._currentSrc = undefined;
            this._started = false;
            this._timeUpdated = false;
            this._currentTime = 0;
            this._currentPlayOptions = undefined;
            this._duration = undefined;
            this._paused = false;
            this._volume = 100;
            this._muted = false;
            this._playRate = 1;
            this._hasConnection = false;

            this._endedPending = false;
            this.onEnded = () => {
                if (!this._endedPending) {
                    this._endedPending = true;
                    this.onEndedInternal();
                }
            };
            this.onTimeUpdate = (time) => {
                if (time && !this._timeUpdated) this._timeUpdated = true;
                this._currentTime = time;
                this._lastTimeSync = Date.now();
                this.events.trigger(this, 'timeupdate');
            };
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
                    this.loading.hide();
                    const dlg = this._videoDialog;
                    // Remove poster so video shows through from subsurface
                    if (dlg) {
                        const poster = dlg.querySelector('.mpvPoster');
                        if (poster) poster.remove();
                    }
                    if (this._currentPlayOptions?.fullscreen) {
                        this.appRouter.showVideoOsd();
                        if (dlg) dlg.style.zIndex = 'unset';
                    }
                    window.api.player.setVideoRectangle(0, 0, 0, 0);
                }
                if (this._paused) {
                    this._paused = false;
                    this.events.trigger(this, 'unpause');
                }
                this.startTimeUpdateTimer();
                this.events.trigger(this, 'playing');
                console.log('[Media] [MPV] playing event triggered');
            };
            this.onPause = () => {
                this._paused = true;
                this.stopTimeUpdateTimer();
                this.events.trigger(this, 'pause');
            };
            this.onError = (error) => {
                this.removeMediaDialog();
                console.error('[Media] media error:', error);
                this.events.trigger(this, 'error', [{ type: 'mediadecodeerror' }]);
            };
            this.onDuration = (duration) => {
                this._duration = duration;
            };
        }

        currentSrc() { return this._currentSrc; }

        async play(options) {
            console.log('[Media] [MPV] play() called with options:', options);
            this._started = false;
            this._timeUpdated = false;
            this._currentTime = null;
            this._endedPending = false;
            if (options.fullscreen) this.loading.show();
            await this.createMediaElement(options);
            console.log('[Media] [MPV] createMediaElement done, calling setCurrentSrc');
            return await this.setCurrentSrc(options);
        }

        getSavedVolume() {
            return this.appSettings.get('volume') || 1;
        }

        setCurrentSrc(options) {
            return new Promise((resolve) => {
                const val = options.url;
                this._currentSrc = val;
                console.log('[Media] [MPV] Playing:', val);

                const ms = (options.playerStartPositionTicks || 0) / 10000;
                this._currentPlayOptions = options;
                this._currentTime = ms;

                const audioIdx = options.mediaSource.DefaultAudioStreamIndex ?? -1;
                const subIdx = options.mediaSource.DefaultSubtitleStreamIndex ?? -1;

                window.api.player.load(val,
                    { startMilliseconds: ms, autoplay: true },
                    { type: 'video' },
                    audioIdx,
                    subIdx,
                    resolve);
            });
        }

        setSubtitleStreamIndex(index) {
            window.api.player.setSubtitleStream(index);
        }

        setSecondarySubtitleStreamIndex(index) {}

        resetSubtitleOffset() {
            window.api.player.setSubtitleDelay(0);
        }

        enableShowingSubtitleOffset() {}
        disableShowingSubtitleOffset() {}
        isShowingSubtitleOffsetEnabled() { return false; }
        setSubtitleOffset(offset) { window.api.player.setSubtitleDelay(Math.round(offset * 1000)); }
        getSubtitleOffset() { return 0; }

        setAudioStreamIndex(index) {
            window.api.player.setAudioStream(index);
        }

        onEndedInternal() {
            this.events.trigger(this, 'stopped', [{ src: this._currentSrc }]);
            this._currentTime = null;
            this._currentSrc = null;
            this._currentPlayOptions = null;
        }

        stop(destroyPlayer) {
            if (!destroyPlayer && this._videoDialog && this._currentPlayOptions?.backdropUrl) {
                const dlg = this._videoDialog;
                const url = this._currentPlayOptions.backdropUrl;
                if (!dlg.querySelector('.mpvPoster')) {
                    const poster = document.createElement('div');
                    poster.classList.add('mpvPoster');
                    poster.style.cssText = `position:absolute;top:0;left:0;right:0;bottom:0;background:#000 url('${url}') center/cover no-repeat;`;
                    dlg.appendChild(poster);
                }
            }
            window.api.player.stop();
            this.onEnded();
            if (destroyPlayer) this.destroy();
            return Promise.resolve();
        }

        removeMediaDialog() {
            window.api.player.stop();
            window.api.player.setVideoRectangle(-1, 0, 0, 0);
            document.body.classList.remove('hide-scroll');
            const dlg = this._videoDialog;
            if (dlg) {
                this.setTransparency(0);
                this._videoDialog = null;
                dlg.parentNode.removeChild(dlg);
            }
        }

        destroy() {
            this.stopTimeUpdateTimer();
            this.removeMediaDialog();
            const player = window.api.player;
            this._hasConnection = false;
            player.playing.disconnect(this.onPlaying);
            player.positionUpdate.disconnect(this.onTimeUpdate);
            player.finished.disconnect(this.onEnded);
            player.updateDuration.disconnect(this.onDuration);
            player.error.disconnect(this.onError);
            player.paused.disconnect(this.onPause);
        }

        createMediaElement(options) {
            let dlg = document.querySelector('.videoPlayerContainer');
            if (!dlg) {
                dlg = document.createElement('div');
                dlg.classList.add('videoPlayerContainer');
                dlg.style.cssText = 'position:fixed;top:0;bottom:0;left:0;right:0;display:flex;align-items:center;background:transparent;';
                if (options.fullscreen) dlg.style.zIndex = 1000;
                document.body.insertBefore(dlg, document.body.firstChild);
                this.setTransparency(2);
                this._videoDialog = dlg;

                const player = window.api.player;
                if (!this._hasConnection) {
                    this._hasConnection = true;
                    player.playing.connect(this.onPlaying);
                    player.positionUpdate.connect(this.onTimeUpdate);
                    player.finished.connect(this.onEnded);
                    player.updateDuration.connect(this.onDuration);
                    player.error.connect(this.onError);
                    player.paused.connect(this.onPause);
                    if (window.jmpNative) {
                        window.jmpNative.notifyRateChange(this._playRate);
                    }
                }
            } else {
                this._videoDialog = dlg;
            }
            if (options.backdropUrl) {
                const existing = dlg.querySelector('.mpvPoster');
                if (existing) existing.remove();
                const poster = document.createElement('div');
                poster.classList.add('mpvPoster');
                poster.style.cssText = `position:absolute;top:0;left:0;right:0;bottom:0;background:#000 url('${options.backdropUrl}') center/cover no-repeat;`;
                dlg.appendChild(poster);
            }
            if (options.fullscreen) document.body.classList.add('hide-scroll');
            return Promise.resolve();
        }

        canPlayMediaType(mediaType) {
            return (mediaType || '').toLowerCase() === 'video';
        }
        canPlayItem(item) { return this.canPlayMediaType(item.MediaType); }
        supportsPlayMethod() { return true; }
        getDeviceProfile(item, options) {
            return this.appHost.getDeviceProfile ? this.appHost.getDeviceProfile(item, options) : Promise.resolve({});
        }
        static getSupportedFeatures() { return ['PlaybackRate', 'SetAspectRatio']; }
        supports(feature) { return mpvVideoPlayer.getSupportedFeatures().includes(feature); }
        isFullscreen() { return window._isFullscreen === true; }
        toggleFullscreen() { window.api?.input?.executeActions(['host:fullscreen']); }

        currentTime(val) {
            if (val != null) { window.api.player.seekTo(val); return; }
            return this._currentTime;
        }
        currentTimeAsync() {
            return new Promise(resolve => window.api.player.getPosition(resolve));
        }
        duration() { return this._duration || null; }
        canSetAudioStreamIndex() { return true; }
        setPictureInPictureEnabled() {}
        isPictureInPictureEnabled() { return false; }
        isAirPlayEnabled() { return false; }
        setAirPlayEnabled() {}
        setBrightness() {}
        getBrightness() { return 100; }
        seekable() { return Boolean(this._duration); }
        pause() { window.api.player.pause(); }
        resume() { this._paused = false; window.api.player.play(); }
        unpause() { window.api.player.play(); }
        paused() { return this._paused; }

        setPlaybackRate(value) {
            this._playRate = +value;
            window.api.player.setPlaybackRate(this._playRate * 1000);
            if (window.jmpNative) window.jmpNative.notifyRateChange(this._playRate);
        }
        getPlaybackRate() { return this._playRate || 1; }
        getSupportedPlaybackRates() {
            return [0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0].map(id => ({ name: id + 'x', id }));
        }

        saveVolume(value) { if (value) this.appSettings.set('volume', value); }
        setVolume(val, save = true) {
            val = Number(val);
            if (!isNaN(val)) {
                this._volume = val;
                if (save) { this.saveVolume(val / 100); this.events.trigger(this, 'volumechange'); }
                window.api.player.setVolume(val);
            }
        }
        getVolume() { return this._volume; }
        volumeUp() { this.setVolume(Math.min(this.getVolume() + 2, 100)); }
        volumeDown() { this.setVolume(Math.max(this.getVolume() - 2, 0)); }
        setMute(mute, triggerEvent = true) {
            this._muted = mute;
            window.api.player.setMuted(mute);
            if (triggerEvent) this.events.trigger(this, 'volumechange');
        }
        isMuted() { return this._muted; }
        togglePictureInPicture() {}
        toggleAirPlay() {}
        getBufferedRanges() { return []; }
        getStats() { return Promise.resolve({ categories: [] }); }
        getSupportedAspectRatios() { return []; }
        getAspectRatio() { return 'normal'; }
        setAspectRatio(value) {}
    }

    window._mpvVideoPlayer = mpvVideoPlayer;
    console.log('[Media] mpvVideoPlayer class installed');
})();
