# Step 5: predictions (written before the code exists)

## The feature

A **data panel** on the left of the view:
- **Objects table:** one row per tracked object; columns id, class, speed, distance, heading. Click a header to sort, filter by class, click a row to select the object (the same selection as clicking it in the view). It updates live, 10 times a second.
- **Event log** (moved here from the lab panel, and taller): severity toggles, a text search, a "follow" switch that keeps the newest event in view, and a count of events kept and shown.

Load range for this step: 50–2,000 objects at 10 Hz, 50–2,000 events per second.

## The first implementation

The obvious one, from the models we already have:
- the table shows `TrackListModel` through a `QSortFilterProxyModel` (sort and class filter); the model resets on every track update (as since step 1);
- the event log shows `EventLogModel` through a second proxy (severity and text); the model resets on every batch of events (20 a second) and keeps every event.

## What step 1 and step 2 already say

- A model reset rebuilds every *visible* delegate (step 1: 1 ms per reset for a 150 px log).
- The 3D view collapses at 2,000 objects (1.5 fps, an 8.5 s stall): every reset makes `Repeater3D` destroy and recreate every box.

## Guesses

| Question | Guess |
|---|---|
| Table, 2,000 objects, first implementation: GUI lag p99 | 6–15 ms (over the 4 ms budget): ~25 visible rows × 5 cells rebuilt 10 times a second, plus a full re-sort |
| Delegates created per second by the table | ~1,250 (125 cells × 10 resets) |
| Event log at 2,000 ev/s: memory | Grows without bound, 50–150 MB per minute |
| Event log at 2,000 ev/s: GUI lag | Over budget: 20 resets per second of a taller list |
| After changing to fine-grained updates (rows changed, inserted, removed instead of reset) | Delegates created per second ≈ 0 at steady state; GUI lag p99 ≤ 4 ms at 2,000 objects |
| Re-sorting 2,000 rows per update in the proxy | < 1 ms |
| Bounded log (newest 10,000 events) | Memory flat; insert/remove at the ends costs < 0.2 ms per batch |
| Text search over 10,000 events | 2–5 ms per keystroke |
| 3D boxes, 2,000 objects, after fine-grained updates (Repeater3D keeps its nodes) | No collapse, but 3–8 ms of GUI work per update (2,000 × several bindings) |
| 3D boxes with instancing (one node, a per-instance table) | < 1 ms per update at 2,000 objects |
| Where the bottleneck goes once the GUI thread is fixed | The render thread (sync of 2,000 instances) or nowhere at this load |
