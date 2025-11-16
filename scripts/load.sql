INSERT INTO
    songs (title, artist, album, filepath)
VALUES
    (
        '往欄印',
        'MyGO!!!!!',
        '往欄印',
        "/Users/wangluyao/Music/my_music/music/MyGO!!!!! - 往欄印 - 1 - 1 - 往欄印.flac"
    );

INSERT INTO
    songs (title, artist, album, filepath)
VALUES
    (
        '不再犹豫',
        'Beyond',
        '犹豫',
        "/Users/wangluyao/Music/my_music/music/Beyond - 犹豫 - 1 - 3 - 不再犹豫.flac"
    );

INSERT INTO
    songs (title, artist, album, filepath)
VALUES
    (
        'Letters',
        '青木陽菜',
        'Letters',
        "/Users/wangluyao/Music/my_music/music/青木陽菜 - Letters - 1 - 12 - Letters.flac"
    );

INSERT INTO
    songs (title, artist, album, filepath)
VALUES
    (
        '正義',
        'ずっと真夜中でいいのに 。',
        '正義',
        "/Users/wangluyao/Music/my_music/music/ずっと真夜中でいいのに。 - 正義 - $unknown$ - $unknown$ - 正義.mp3"
    );

INSERT INTO
    playlists
VALUES
    (1, "Default Playlist", 3);

INSERT INTO
    playlist_items(playlist_id, song_id, position)
VALUES
    (1, 2, 1);

INSERT INTO
    playlist_items(playlist_id, song_id, position)
VALUES
    (1, 3, 1);

INSERT INTO
    playlist_items(playlist_id, song_id, position)
VALUES
    (1, 1, 1);

INSERT INTO
    playlist_items(playlist_id, song_id, position)
VALUES
    (1, 4, 1);