(function() {
    class mpvAudioPlayer {
        constructor(opts) {
            this.name = 'MPV Audio Player';
            this.type = 'mediaplayer';
            this.id = 'mpvaudioplayer';
            this.priority = -1;
            this.isLocalPlayer = true;
        }
        canPlayMediaType(mediaType) {
            return (mediaType || '').toLowerCase() === 'audio';
        }
        canPlayItem(item) { return this.canPlayMediaType(item.MediaType); }
    }
    window._mpvAudioPlayer = mpvAudioPlayer;
    console.log('[Media] mpvAudioPlayer plugin installed');
})();
