@page file_formats Binary file formats

* Little-endian throughout
* Length-prefixed string: u32 byte length, then that many bytes.
* Vector2: 2 * f32 (x, y)
* Vector3: 3 * f32 (x, y, z)
* Polygon: u32 vertex count, then that many Vector3

---

- @subpage bsp
- @subpage vis
- @subpage rad
- @subpage filegeo