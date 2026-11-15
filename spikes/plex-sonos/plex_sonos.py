#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["soco>=0.32", "plexapi>=4.15"]
# ///
"""Spike: browse a Plex music library and queue it on a Sonos speaker.

Throwaway code that answers one question for the SonosController design: which
way of handing Plex music to a Sonos queue works on the real speakers.

  --via service   through the Plex music service linked in the Sonos app
                  (SoCo's PlexPlugin; what Home Assistant does)
  --via url       as plain stream addresses on the Plex server, one per track

Settings come from the environment or from a `.env` file next to this script:

  SONOS_IP      address of any one speaker
  PLEX_URL      e.g. http://192.168.1.10:32400 (must be reachable from the
                speakers for --via url)
  PLEX_TOKEN    a Plex token
  PLEX_LIBRARY  name of the music library (optional when there is only one)

Examples:

  plex_sonos.py speakers
  plex_sonos.py artists
  plex_sonos.py albums "Stevie Wonder"
  plex_sonos.py add album "Stevie Wonder" "Innervisions" --play
  plex_sonos.py add playlist "Workshop" --via url
  plex_sonos.py queue
  plex_sonos.py clear
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


def load_dotenv() -> None:
    path = Path(__file__).resolve().parent / ".env"
    if not path.exists():
        return
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip("'\""))


def setting(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        sys.exit(f"{name} is not set (environment or .env next to this script)")
    return value


# --- Sonos -----------------------------------------------------------------


def speaker(args):
    """The speaker to control: the coordinator of the chosen speaker's group."""
    import soco

    device = soco.SoCo(setting("SONOS_IP"))
    if args.speaker:
        matches = [z for z in device.all_zones if z.player_name.lower() == args.speaker.lower()]
        if not matches:
            names = ", ".join(sorted(z.player_name for z in device.all_zones))
            sys.exit(f"No speaker named {args.speaker!r}. Known: {names}")
        device = matches[0]
    return device.group.coordinator


def cmd_speakers(args):
    import soco

    seed = soco.SoCo(setting("SONOS_IP"))
    for zone in sorted(seed.all_zones, key=lambda z: z.player_name):
        coordinator = zone.group.coordinator
        members = sorted(m.player_name for m in zone.group.members)
        role = "coordinator" if coordinator.uid == zone.uid else f"follows {coordinator.player_name}"
        visible = "" if zone.is_visible else "  (hidden)"
        print(f"{zone.player_name:24} {zone.ip_address:15} {role:28} group={members}{visible}")


def cmd_status(args):
    device = speaker(args)
    transport = device.get_current_transport_info()
    track = device.get_current_track_info()
    print(f"speaker   {device.player_name} ({device.ip_address})")
    print(f"state     {transport['current_transport_state']}")
    print(f"volume    {device.volume}")
    print(f"queue     {device.queue_size} items, at #{track.get('playlist_position')}")
    print(f"title     {track.get('title')}")
    print(f"artist    {track.get('artist')}")
    print(f"album     {track.get('album')}")
    print(f"position  {track.get('position')} / {track.get('duration')}")
    print(f"art       {track.get('album_art')}")
    print(f"uri       {redact(track.get('uri') or '')}")


def cmd_queue(args):
    device = speaker(args)
    items = device.get_queue(max_items=1000)
    for index, item in enumerate(items, start=1):
        creator = getattr(item, "creator", "") or ""
        album = getattr(item, "album", "") or ""
        uri = item.resources[0].uri if item.resources else ""
        line = f"{index:4}  {item.title}  |  {creator}  |  {album}"
        if args.uris:
            line += f"\n      {redact(uri)}"
            line += f"\n      art: {redact(getattr(item, 'album_art_uri', '') or '')}"
        print(line)
    print(f"{len(items)} items")


def cmd_clear(args):
    device = speaker(args)
    device.clear_queue()
    print("queue cleared")


def cmd_transport(args):
    device = speaker(args)
    {"play": device.play, "pause": device.pause, "next": device.next, "prev": device.previous}[args.command]()
    print(device.get_current_transport_info()["current_transport_state"])


def cmd_volume(args):
    device = speaker(args)
    if args.level is not None:
        device.volume = args.level
    print(device.volume)


# --- Plex ------------------------------------------------------------------


def plex_server():
    from plexapi.server import PlexServer

    return PlexServer(setting("PLEX_URL"), setting("PLEX_TOKEN"))


def music_library(plex):
    name = os.environ.get("PLEX_LIBRARY")
    if name:
        return plex.library.section(name)
    sections = [s for s in plex.library.sections() if s.type == "artist"]
    if len(sections) != 1:
        names = ", ".join(s.title for s in sections) or "none"
        sys.exit(f"Set PLEX_LIBRARY; music libraries on this server: {names}")
    return sections[0]


def cmd_artists(args):
    music = music_library(plex_server())
    artists = music.all()
    for artist in artists:
        print(artist.title)
    print(f"{len(artists)} artists in {music.title!r}")


def cmd_albums(args):
    music = music_library(plex_server())
    for album in music.get(args.artist).albums():
        print(f"{album.year or '----'}  {album.title}")


def cmd_tracks(args):
    music = music_library(plex_server())
    for track in music.get(args.artist).album(args.album).tracks():
        part = track.media[0].parts[0]
        media = track.media[0]
        print(f"{track.index or 0:3}  {track.title}  [{media.audioCodec} {part.container} {media.bitrate}kbps]")


def cmd_playlists(args):
    plex = plex_server()
    for playlist in plex.playlists(playlistType="audio"):
        kind = "smart" if playlist.smart else "plain"
        print(f"{playlist.title}  ({playlist.leafCount} tracks, {kind})")


def resolve(plex, args):
    """The plexapi object named on the command line."""
    kind, names = args.kind, args.names
    expected = {"artist": 1, "album": 2, "track": 3, "playlist": 1}[kind]
    if len(names) != expected:
        usage = {"artist": "ARTIST", "album": "ARTIST ALBUM", "track": "ARTIST ALBUM TRACK", "playlist": "NAME"}
        sys.exit(f"add {kind} takes: {usage[kind]}")
    if kind == "playlist":
        return plex.playlist(names[0])
    artist = music_library(plex).get(names[0])
    if kind == "artist":
        return artist
    album = artist.album(names[1])
    if kind == "album":
        return album
    return album.track(names[2])


def tracks_of(item):
    if item.TYPE == "track":
        return [item]
    if item.TYPE == "playlist":
        return item.items()
    return item.tracks()


def cmd_add(args):
    plex = plex_server()
    item = resolve(plex, args)
    device = speaker(args)
    before = device.queue_size

    if args.via == "service":
        from soco.plugins.plex import PlexPlugin

        first = PlexPlugin(device).add_to_queue(item)
    else:
        first = add_as_urls(plex, device, tracks_of(item))

    after = device.queue_size
    print(f"added {item.TYPE} {item.title!r} via {args.via}: {after - before} tracks, first at #{first}")
    if args.play:
        device.play_from_queue(first - 1)
        print("playing")


def add_as_urls(plex, device, tracks):
    """Queue each track as a direct stream address on the Plex server."""
    from soco.data_structures import DidlMusicTrack, DidlResource

    mime = {"mp3": "audio/mpeg", "flac": "audio/flac", "mp4": "audio/mp4", "m4a": "audio/mp4",
            "ogg": "audio/ogg", "wav": "audio/wav", "aiff": "audio/aiff"}
    first = None
    for track in tracks:
        part = track.media[0].parts[0]
        uri = plex.url(part.key, includeToken=True)
        art = plex.url(track.thumb, includeToken=True) if track.thumb else None
        protocol = f"http-get:*:{mime.get(part.container, 'audio/mpeg')}:*"
        didl = DidlMusicTrack(
            title=track.title,
            parent_id="-1",
            item_id="-1",
            resources=[DidlResource(uri=uri, protocol_info=protocol)],
            creator=track.originalTitle or track.grandparentTitle,
            album=track.parentTitle,
            album_art_uri=art,
        )
        position = device.add_to_queue(didl)
        first = first or position
    if first is None:
        sys.exit("nothing to add: no tracks")
    return first


def redact(text: str) -> str:
    token = os.environ.get("PLEX_TOKEN")
    return text.replace(token, "<token>") if token else text


# --- Command line ----------------------------------------------------------


def main():
    load_dotenv()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--speaker", help="speaker name (default: the one at SONOS_IP)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("speakers", help="list the speakers and their groups").set_defaults(func=cmd_speakers)
    sub.add_parser("status", help="what is playing").set_defaults(func=cmd_status)
    queue = sub.add_parser("queue", help="show the queue")
    queue.add_argument("--uris", action="store_true", help="also show each item's address and artwork")
    queue.set_defaults(func=cmd_queue)
    sub.add_parser("clear", help="clear the queue").set_defaults(func=cmd_clear)
    for name in ("play", "pause", "next", "prev"):
        sub.add_parser(name).set_defaults(func=cmd_transport)
    volume = sub.add_parser("volume", help="show or set the volume")
    volume.add_argument("level", nargs="?", type=int)
    volume.set_defaults(func=cmd_volume)

    sub.add_parser("artists", help="list the artists in Plex").set_defaults(func=cmd_artists)
    albums = sub.add_parser("albums", help="list an artist's albums")
    albums.add_argument("artist")
    albums.set_defaults(func=cmd_albums)
    tracks = sub.add_parser("tracks", help="list an album's tracks")
    tracks.add_argument("artist")
    tracks.add_argument("album")
    tracks.set_defaults(func=cmd_tracks)
    sub.add_parser("playlists", help="list the audio playlists in Plex").set_defaults(func=cmd_playlists)

    add = sub.add_parser("add", help="add an artist, album, track or playlist to the queue")
    add.add_argument("kind", choices=["artist", "album", "track", "playlist"])
    add.add_argument("names", nargs="+", help="ARTIST [ALBUM [TRACK]] or the playlist NAME")
    add.add_argument("--via", choices=["service", "url"], default="service")
    add.add_argument("--play", action="store_true", help="start playing at the first added track")
    add.set_defaults(func=cmd_add)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
