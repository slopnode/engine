@page navigation_model Navigation

Ground and flying actors path over a leaf/portal graph rather than searching the raw BSP tree or line-tracing every frame. The walkable world is reduced to a graph of convex regions connected by portals, and pathing is a graph search over that. See the Navigation section of @ref scriptingapi for the actor-facing script calls.

# Graph {#navigation-graph}

Two ways to build the walkable graph, sharing the same on-disk and in-memory shape so pathing code doesn't care which one produced it:

- BSP leaf graph: derived straight from the compiled map's sealed interior leaves and their shared portals. Always available with no separate bake step, but inherits the BSP's own convex-decomposition granularity. A room built out of many small or overlapping brushes compiles to many small leaves and narrow portals.
- Baked navmesh: `slopnav` voxelizes the map's walkable static geometry into a much smaller number of merged polygons, sized to an actor's radius, height, step height, and walkable slope. This is what a finished map should ship with. A whole staircase collapses into one walkable run instead of one leaf per step, and routes read straighter and cheaper as a result.

Both flavors store the same per-edge information: a portal center, a tangent and half-width across the portal's widest span, a routing cost, an optional gating door, and the true vertical rise across that specific edge. Waypoint smoothing, door gating, and climb checks all run unmodified against either one. See @ref nav for the file layout.

# Baking {#navigation-baking}

`slopnav` needs the map's compiled BSP and carved geometry to already exist, so it always runs after `slopbsp`/`slopcsg`, and it refuses to bake anything for a leaking, unsealed hull. It bakes once per entry in a package's movement-profile catalog, each profile naming an agent radius, height, step height, and walkable slope. A package that hasn't defined any profiles still gets exactly one bake, using the same defaults the character controller itself uses, so an older map keeps working unchanged. Each profile writes its own file under the map's compiled navigation folder. An actor whose own body doesn't match the smallest baked profile, a big monster on a map only baked for a person-sized default, paths against a bake sized for it instead of one too tight for its body.

Door corridors are left out of the walkable voxelization itself, so a closed door doesn't carve a wall-shaped hole into the mesh, and are re-tagged onto the resulting graph afterward by testing each edge against the door's own bounds. Water volumes are tagged the same way, by testing against water brush volumes.

# Pathfinding {#navigation-pathfinding}

An actor's live route comes from a graph search from its current region to the goal region. When several actors want the same goal, they share one precomputed field over the whole graph instead of each running its own search, so a crowd chasing the same target costs about the same as pathing one actor.

A link behind a closed door is skipped, same as an unwalkable region. A link whose vertical rise exceeds an actor's own step height is skipped too, so a small monster can't take a route only a tall one could climb. Descending is never blocked outright. An unusually large drop instead adds a routing cost penalty, and a route through water gets its own cost penalty. Both preferences bias which route looks cheaper without ever making a route flatly unreachable.

# Waypoints {#navigation-waypoints}

A raw region-to-region path is a sequence of areas, not a walkable line. Placing one waypoint at each portal's center is the obvious way to turn it into one, but on a room built from a lot of small regions that produces a visible zigzag even where a straight line would do. Waypoints are instead pulled taut against each portal's edges, the same string-pulling most navmesh systems use, anchored at the actor's real position so the pull starts from where it's actually standing rather than from its current region's centroid. A small per-actor sideways offset can nudge each waypoint off-center along its portal without pushing it past the portal's edge, so a group of actors sharing a route don't all converge on the exact same point and shoulder each other along the way.

# Fragmented corridors {#navigation-macro-links}

A corridor built from a lot of thin or overlapping brushwork, a staircase made of one box per step is the common case, can stay fragmented into a lot of tiny regions even after waypoint smoothing softens the path through them. Routing through that many hops is slower than it needs to be. A separate offline pass looks for exactly this shape: a cluster of small regions that only touches the rest of the walkable graph through two other, larger regions. It validates a candidate by literally driving a throwaway body through it with real physics, once per movement profile. A cluster a given profile can actually walk end to end becomes a single shortcut edge for that profile, replacing however many tiny hops used to be needed to cross it. A cluster that fails, too narrow for that profile or the walk-through gets stuck, just doesn't get a shortcut. Routing falls back to the untouched fine-grained graph.

# Replanning {#navigation-replanning}

Each actor with a goal replans on a fixed interval rather than every frame, plus immediately whenever its goal moves, its current region changes to somewhere its planned route didn't expect, or a stuck check trips. Stuck detection watches for an actor that hasn't made meaningful progress toward its current waypoint for a short window. This is usually a physics obstruction the graph has no way to know about, like another actor blocking a doorway. It forces an early replan rather than waiting out the interval. A region change that still lies on the remaining planned route is cheap to confirm and skips a full search. If a replan can't find any route to the current goal at all, the actor keeps following its last known-good route rather than freezing in place, on the assumption that a temporarily unreachable goal is more common than a permanently invalid one.

# Debugging {#navigation-debugging}

The in-game debug menu's Nav Paths toggle draws every active actor's planned waypoints and current region over the world, and verbose logging behind the same toggle traces every replan decision and stuck detection per actor. `slopnav --compare` bakes both graph flavors for a map side by side and prints hop count and route cost between two far-apart points, useful for sanity-checking a navmesh bake against the BSP fallback it's meant to replace.
