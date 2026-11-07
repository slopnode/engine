@page networking_design Networking design (draft)

@note This is a design draft. The transport interface with its in-memory test backend, its ENet socket backend and its encrypted layer, declared state and events, and a first pass of playing over the network with one client, are implemented. For declared state, `*net-state*` and `*net-extension*` load from `data/net.s7`, the engine stores the fields, and `net-get`, `net-set!`, `net-reset!`, `net-save-state` and `net-restore-state!` read and write them (see @ref dataapi and @ref scriptingapi). The `send` option decides which player fields reach the client. Several players, sessions, transfers and the coordinator are not implemented.

Multiplayer in slopengine is server-authoritative. A server runs the simulation for a session and clients send it their `PlayerCommand` each tick (see @ref ecs_pipeline). Everything else on this page is about the two questions that sit above the wire format. What state does a game have, and who gets to see it. And how players come together, move between sessions and leave, without the engine deciding on one shape such as a server cycling through a map list.

The engine provides the mechanics: typed state storage, replication, events, sessions, transfers, joins and listings. The base package declares its state and its session kinds as data, and its scripts drive the flow. A map rotation is something a game script can do, not something the engine knows about.

# Transport

The engine talks to the network only through `NetTransport` (`src/net/transport.hpp`), which follows the message model of Valve's GameNetworkingSockets so a backend maps onto it one to one. A server calls `listen`, gets an `Incoming` event per connection attempt and calls `accept` or `close`. A client calls `connect`. Both sides then get `Connected`. Messages are whole byte buffers of at most 512 KiB, sent `Reliable`, which arrive once and in order, or `Unreliable`, which can be lost or arrive out of order. `poll` drains every event that has arrived, and `close` sends a reason the peer receives with its `Disconnected` event.

`MemoryNetwork` (`src/net/memory_transport.hpp`) is an in-process backend for tests. Every endpoint it hands out shares one simulated network with a manual clock, `advance`, and a seeded random source, so a test is repeatable. `setConditions` adds latency, jitter and loss. Unreliable messages are dropped at the loss rate and jitter can reorder them. Reliable messages are never dropped, a loss adds a resend delay instead, and they stay in order. `createEnetTransport` (`src/net/enet_transport.hpp`) is the socket backend, built on ENet, which every build includes. Addresses are `host:port` for `connect` and `port` or `host:port` for `listen`. ENet accepts connections on its own, so the backend keeps the accept step on top of it: the server sees `Incoming` and holds the peer, and the client only sees `Connected` once `accept` has sent it a reliable confirmation. An ENet disconnect carries no text, so `close` sends its reason as a last reliable message. A connection attempt that gets no answer fails with the reason `no response` after `EnetTransportOptions::connectTimeoutMs`. ENet sends plain UDP over IPv4, without encryption, which suits LAN play, self-hosted servers and development. Several transports can share one process, which is how its test runs a client and a server together.

# Encryption

`SecureTransport` (`src/net/secure_transport.hpp`) wraps any transport, such as `createSecureTransport(createEnetTransport())`, and offers the same interface with every message encrypted and authenticated. It uses Monocypher, and the handshake follows the shape of the Noise protocol patterns where the client knows or learns the server's key.

A server has a long-term X25519 key pair. `SecureTransportOptions::staticSecret` sets it, otherwise `listen` generates one. A dedicated server that wants a stable identity across restarts keeps its secret and passes it in. Its public key is `publicKey()`, and `secureKeyFingerprint` turns a key into a short hex string for people to compare.

The handshake runs inside the wrapped transport's connection, before `Connected` is reported.

1. The client sends a fresh ephemeral public key.
2. The server sends its long-term public key, a fresh ephemeral public key, and a MAC made with the new keys.
3. The client checks the server's key against `SecureTransportOptions::pinnedServerKey` when one is set, checks the MAC, and answers with a MAC of its own.

Both sides derive the session keys from two X25519 exchanges, ephemeral with ephemeral and the client's ephemeral with the server's long-term key, mixed by BLAKE2b with a hash of the keys exchanged. Only the holder of the server's long-term secret can finish the handshake, and the ephemeral exchange means a stolen long-term key doesn't reveal earlier sessions. A client without a pinned key learns the server's key from `peerKey` and can remember it, the way SSH remembers hosts.

Each direction has its own key. Messages are sealed with XChaCha20-Poly1305 and carry a counter that forms the nonce, which adds 26 bytes to each message. A reliable message has to carry exactly the next counter, so a replayed, dropped or altered one closes the connection with `bad message`. An unreliable message passes through a 64-message replay window, the way WireGuard does it, so a late one is accepted once and a copy or a forgery is dropped.

This protects message contents from being read or changed on the way, and stops a server being impersonated once its key is known. It does not identify clients, which is left to whatever accounts or tokens a game adds on top. Close reasons travel in plain text through the wrapped transport. The protocol is the engine's own composition of standard primitives and has not been independently reviewed.

# Playing over the network

`NetSession` (`src/net/net_session.hpp`) runs a game over the encrypted ENet transport. Start a server and a client with the base package's own map flag.

```
slopengine --game slopdemo --dedicated --listen 27960 --map demoroom
slopengine --game slopdemo --connect 127.0.0.1:27960
```

The client sends a hello with the protocol, the engine version, every mounted package with its version, and the schema hash of the declared state. The server refuses a client whose hello doesn't match and sends the reason, such as `declared state differs`. A server takes one client at a time and answers a second with `server full`.

The server gives every replicated entity a number: actors, things, movers, motored bodies and debris that sit at the root of the hierarchy and have a name. It introduces each number to the client with the entity's name, and the client binds it to its own entity of that name. Names match because both sides load the same map and run the same `on-map-ready`. An entity only the server creates, such as a rocket, has no match on the client and isn't shown yet.

The server tells the client which map to load and sends it again after every map change. Each tick it sends a snapshot: the position and rotation of every replicated entity, its animation clip and time, the player's eye position, and the declared player fields whose `send` is `all` or `owner`. The client runs no simulation. It feeds each snapshot into the same blending that smooths ticks, so it draws one snapshot behind. It builds a `PlayerCommand` sixty times a second from local input and sends it with the three before it, so a lost packet doesn't lose a press. The server applies the commands to the map's player in order, merges presses that arrive together, and turns the player to the client's view angles.

This is a first pass. The client's own movement waits a round trip because nothing predicts it. Sounds and effects reach the client as far as the game raises them as events. Anything else scripts do in `tick` doesn't, since the client runs no ticks, so a client in slopdemo hears its gun and sees tracers and blood but sees no first-person weapon. With `--listen` and no `--dedicated`, the connected client drives the host's player and the host's own input is ignored.

# Principles

State is declared, not discovered. The engine never inspects Scheme values to work out what to send. A base package lists every replicated field with a type and a size, so the server knows the layout of every message before the first client connects, and a client with the same packages builds the same layout.

The engine stores declared state. Fields live in components on entities and on the session, not in Scheme globals. Scripts read and write them through engine functions. That makes the state per player by construction, lets the engine track what changed, and lets the same schema back save games.

Only the server writes. In client mode the setters refuse. Authority lives in the engine functions, not in each game's discipline.

Mods extend within a budget the base package sets. A mod can declare extra fields and events in its own namespace. It never gets a socket, a connection or a session function. Declaring a field is data, and writing one is ordinary `world-mutate`.

Topology belongs to the game. Session kinds, join rules, roles and what travels with a player are declared per game, and the scripts decide when sessions are created, when players move and when a map changes.

# Files

Each package may ship `data/net.s7`. The engine loads the base package's file at boot, then each mod's in mount order, the same order as `data/actions.s7` and `data/things.s7`. The base file may define four symbols.

Symbol|Defines
-|-
`*net-state*`|Replicated fields per scope
`*net-events*`|One-shot events with typed arguments
`*net-extension*`|How many fields and events mods may add, per scope
`*net-sessions*`|Session kinds with their join rules, role names and listing fields

A mod's file may define `*net-state*` and `*net-events*` only. Its names are prefixed with the mod's package id, so a field `stamina` from package `staminamod` is `staminamod/stamina` everywhere.

# State

`*net-state*` is an alist from scope to field list. A scope says where the fields live.

Scope|Lives on
-|-
`player`|Each player entity
`session`|The session as a whole, such as round state, score or progress through a mission
`(thing tag)`|Each thing whose def carries `tag`, such as `(thing monster)`

Each field has a name, a type and options.

<pre><code class="language-scheme">(health (type int 0 200) (default 100) (send all) (travel \#t))
</code></pre>

## Types

Type|Meaning
-|-
`bool`|True or false
`(int lo hi)`|Integer in a range, packed into as few bits as the range needs
`(real lo hi step)`|Number quantized to `step`, such as `(real 0 1 1/256)`
`(symbol a b c ...)`|One of a fixed set of symbols
`(string n)`|String of at most `n` bytes
`entity`|Reference to a replicated entity, sent as its network id
`(list type n)`|Up to `n` values of `type`
`(table key-type value-type n)`|Up to `n` key and value pairs
`float`|Any finite number, sent as 32 bits

Nothing else is allowed, in particular no procedures and no arbitrary nested Scheme data. Every field has a maximum encoded size the loader can compute, which is how budgets are checked.

## Options

Option|Values|Meaning
-|-|-
`default`|a value of the type|Value for a new player, session or thing
`send`|`all`, `owner`, `none`|Who receives the field. `owner` is only the player the entity belongs to. `none` stays on the server
`travel`|`\#t`, `\#f`|Whether the field goes with a player who moves to another map or session. See @ref net-travel
`save`|`\#t`, `\#f`|Whether save games store the field. Defaults to the value of `travel`

`send` is also the anti-cheat line. A field a client never receives cannot be read off that client, so where enemies are, what a hidden door's state is or a monster's health stay `none` unless the game wants them shown.

The engine has no notion of teams or parties. If a game later needs to send a field to some players but not others, the plan is plain named groups that server scripts fill, such as `(net-group-add! 'red player-id)`, and a `(send (group red))` audience. The engine would only know which players are in a set. What a group means stays in the game's scripts.

# Events

Events are one-shot messages that don't belong in state, such as a sound, a hit marker or a pickup flash. Scripts on the server raise them, the engine sends them, and clients run a handler for them. They replace scripts calling sound and effect functions directly in the tick, which on a dedicated server reach no one.

<pre><code class="language-scheme">(pickup (args (item (string 24))) (to owner) (reliable \#t))
</code></pre>

Option|Values|Meaning
-|-|-
`args`|name and type pairs|Typed arguments, using the same types as fields
`to`|`all`, `owner`, `(near metres)`, `pvs`|Who receives it. `near` and `pvs` measure from the event's position
`reliable`|`\#t`, `\#f`|Whether a lost packet is resent. Cosmetic events usually are not

A script raises an event with `(net-event! name where arg ...)`. `where` is a point `(x y z)`, a player id or `\#f`, and the arguments follow the declared order and types. Every process that receives the event, the raising one included, runs `(on-event-NAME where arg ...)` when the base package or a mod defines it, the way package actions run `on-action-NAME`. So a game moves an effect onto an event by raising the event where the effect used to happen and doing the effect in the handler, and single-player keeps working unchanged. On a server the event also goes to the client when the audience allows it. `owner` needs `where` to be the client's player, `near` needs the client's player within the radius of `where`, and `pvs` currently reaches everyone.

A single-player game raises events the same way, and they are delivered locally in the same tick, so game scripts don't branch on whether they are networked.

# Mod extension

`*net-extension*` sets the budget per scope. A scope that is not listed accepts no mod fields.

<pre><code class="language-scheme">(define *net-extension*
  '((player (fields 8) (bytes 32))
    (session (fields 4) (bytes 16))
    (events (count 4) (bytes 64))))
</code></pre>

The loader adds every mod's fields up against the budget in mount order and refuses the mod whose fields overflow it, with the reason in the log. Field and event ids are assigned in a fixed order, sorted by package id and then by name, so a server and a client with the same packages always agree. The schema hash covers mod fields, so a client missing a mod the server runs is turned away at connect with the mod's id and version, not by a snapshot it can't read.

# Sessions {#net-sessions}

A session is one authoritative world: one map at a time, the players in it, and its `session` fields. A server process hosts one session at a time, since one process holds one world. A coordinator, which is part of the engine and not of any game, keeps track of sessions, places new ones on server processes and tells clients where to connect. Moving a player between sessions on different machines is the same operation as moving them between two sessions on one.

`*net-sessions*` declares the kinds of session a game uses. The engine enforces what is declared here. Everything about when and why is left to scripts.

Option|Meaning
-|-
`players`|Minimum and maximum members
`join`|How new members get in, see below
`late-join`|Whether members can join after the session started its map
`roles`|Names of the roles a member can be given. Scripts assign them, see below
`announce`|`listed` appears in server and lobby lists, `hidden` can be found by matchmaking queries only, `private` by invite only
`listing`|Fields a listing or a matchmaking query can see, such as the map, the mission or a level range

`join` is a list of ways in, each with a limit.

Way in|Meaning
-|-
`(open n)`|Anyone may join, up to `n`
`(invite n)`|Members may invite, up to `n`
`(summon n)`|The session owner accepts a request from a player who offered to help, up to `n`
`(invasion n)`|Matchmaking may place an uninvited player, up to `n`, if the owner allows it

A role is a label. A script gives a member one with `session-set-role!`, usually in `on-player-join` based on how they got in, and the engine passes it to hooks and shows counts per role in listings. What a role allows, such as whether it counts as an enemy, is game code.

## Hooks

Hook|Called when
-|-
`(on-player-join player-id how)`|A member enters. `how` is the way in, such as `invite` or `invasion`. The game gives them a role, spawns them, announces them and decides what they may do
`(on-player-leave player-id reason)`|A member leaves, disconnects or is moved away. The game decides whether their body stays, turns into an AI or vanishes
`(on-session-start kind)`|The session's first map is ready
`(on-transfer-in player-id record from-kind reason)`|A member arrives from another session. `record` is their travel record, already checked against the schema. Nothing in it is applied until the script applies it

Late join and sudden departure are normal events, not special cases. A joiner receives the full current state of every replicated entity and every field it is allowed to see before its first tick. Scripts therefore can't assume the party is fixed at map start, and must handle a member vanishing mid-fight. Join and leave hooks are where that is handled.

## Travel {#net-travel}

Moving a player to another map in the same session, or to another session anywhere, carries a travel record: the values of their `travel` fields, encoded with the schema. Only servers build and read travel records. A client never sends one, so a client can't arrive somewhere with an inventory it invented. A map change inside one session keeps every `travel` field as it is, since the same server stays in charge. slopdoom's `carry` map loads, which keep health, ammo and inventory between maps today, are this case.

A transfer between sessions doesn't apply anything by itself. The engine checks the record against the schema and hands it to `on-transfer-in` in the receiving session, and that script decides what to take. It can apply the whole record with `net-apply-travel!`, apply a list of fields, or read values out of it and do something else. The engine guarantees only that the record is well formed and came from a server. Coming back from a visit is a transfer like any other. A player's home session gets the record the visited session built, and its own script decides which of those values to accept.

## Script functions

These need a new capability, `session-control`, held by engine and base package code only. Mods can read session fields and listings, and can register join and leave hooks with `hook-add`.

Function|Does
-|-
`(net-get scope-id field)`|Reads a field. `scope-id` is a player id, a thing id or `'session`
`(net-set! scope-id field value)`|Writes a field. Server only, checked against the field's type
`(net-event! name position-or-player args ...)`|Raises an event
`(session-create kind options)`|Asks the coordinator for a new session. Returns its id once it exists
`(session-transfer player-id session-id reason)`|Moves a member, with their travel record
`(session-set-role! player-id role)`|Gives a member one of the kind's roles
`(net-apply-travel! player-id record [fields])`|Applies a travel record, or only the named fields of it
`(session-change-map map-id reason)`|Changes the current session's map for every member
`(session-set-listing field value)`|Updates what listings and matchmaking see
`(session-close reason)`|Ends the session and returns members to wherever the game sends them

# Topologies

The same declarations cover very different games. The engine sees no difference between these, only the scripts do.

A dedicated server with a map rotation is a single `listed` session kind with `(open 16)`. The game script keeps the rotation and calls `session-change-map` at the end of each round.

A co-op campaign is a `private` kind with `(invite 3)`. When a mission ends, the host's script calls `session-change-map` for the next map in the mission, or `session-create` plus `session-transfer` for each member to continue on another server, for example one closer to the party.

A hub with rooms is a `listed` hub kind with `(open 64)` and a `listed` room kind. Players walk into a room's portal, and the hub script transfers them into the room's session. The room's `listing` fields are what the hub shows on its board.

A drop-in world in the style of Dark Souls is a `hidden` kind per player, owned by that player, with `(summon 2)` and `(invasion 1)` and `late-join \#t`. Its `listing` holds the area and a level range, which is what matchmaking uses to place summoned helpers and invaders. `on-player-join` gives a summoned helper the `phantom` role and an uninvited one the `invader` role, puts them somewhere sensible and announces them. The game's targeting code treats invaders as enemies. `on-player-leave` handles a visitor dying or the owner beating them. When a phantom returns home, the home world's `on-transfer-in` takes the souls out of the record and ignores the rest, so nothing else that happened in someone else's world changes the helper's own game.

On Steam these map onto lobbies. `listed` and `hidden` are lobby visibility, listing fields are lobby metadata that lobby searches filter on, `invite` is a Steam invite, and the coordinator's placement uses Steam's dedicated server and relay support. Another backend maps the same declarations onto its own services.

# Example

This is what slopdoom's `data/net.s7` could look like. The player fields are the ones `*player-state*` holds today through `state-get` and `state-put!`, and `picked-things` and the mission are session state. Moving slopdoom onto it means replacing those two functions with `net-get` and `net-set!` on the player, and the save form with the schema's `save` fields.

<pre><code class="language-scheme">(define *net-state*
  '((player
     (health (type int 0 200) (default 100) (send all) (travel \#t))
     (armor (type int 0 200) (default 0) (send owner) (travel \#t))
     (weapon-slot (type int 1 9) (default 2) (send all) (travel \#t))
     (weapon-owned (type (list (int 1 9) 9)) (default (1 2)) (send owner) (travel \#t))
     (weapon-ammo (type (table (int 1 9) (int 0 300) 9)) (default ((2 . 50))) (send owner) (travel \#t))
     (inventory (type (table (string 24) (int 0 99) 32)) (default ()) (send owner) (travel \#t))
     (dead (type bool) (default \#f) (send all) (travel \#f)))
    (session
     (mission (type (string 32)) (default "") (send all) (save \#t))
     (difficulty (type (symbol easy normal hard nightmare)) (default normal) (send all) (save \#t))
     (picked-things (type (list (string 32) 512)) (default ()) (send none) (save \#t)))
    ((thing monster)
     (health (type int 0 4000) (default 100) (send none)))))

(define *net-events*
  '((pickup (args (item (string 24))) (to owner) (reliable \#t))
    (hurt (args (amount (int 0 255)) (from entity)) (to owner) (reliable \#f))
    (weapon-fire (args (slot (int 1 9))) (to (near 40)) (reliable \#f))
    (secret-found (args) (to all) (reliable \#t))))

(define *net-extension*
  '((player (fields 8) (bytes 32))
    (session (fields 4) (bytes 16))
    (events (count 4) (bytes 64))))

(define *net-sessions*
  '((campaign
     (players 1 4)
     (join (invite 3))
     (late-join \#t)
     (announce private)
     (roles host guest)
     (listing (mission (string 32)) (difficulty (symbol easy normal hard nightmare))))
    (deathmatch
     (players 2 16)
     (join (open 16))
     (late-join \#t)
     (announce listed)
     (roles player)
     (listing (map (string 32)) (players (int 0 16))))))
</code></pre>

A mod adding stamina declares only its own field, which the loader names `staminamod/stamina` and counts against the eight player fields the budget allows.

<pre><code class="language-scheme">(define *net-state*
  '((player
     (stamina (type (real 0 1 1/64)) (default 1) (send owner) (travel \#t)))))
</code></pre>

# Open questions

How a thing's network id is assigned when a map places it and the server later respawns it, so a late joiner and an early one agree.

How the coordinator is run for small games: inside the first server process, as its own process, or entirely through a platform service such as Steam lobbies.

What a client's view of `session` fields is while it is being transferred and has left one session but not yet joined the next.
