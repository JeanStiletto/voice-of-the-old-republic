namespace Kdev;

// ----------------------------------------------------------------------
// BWM v1.0 walkmesh reading — the shared header knowledge.
//
// Three commands independently reimplemented this: the geometry audit, the
// walkmesh stats command, and the face-types dump. The geometry file even
// carried a comment saying "kept duplicated intentionally; hoist only if a
// 3rd consumer appears" — by the time the Phase-2 duplication scan looked,
// the third consumer already existed, so the file's own stated trigger had
// been met.
//
// Header layout (offsets verified identical across all three former
// copies):
//   0x00  magic "BWM V1.0"
//   0x08  type uint32           (1 = area walkmesh, 0 = placeable/door)
//   0x48  vertexCount uint32
//   0x4C  vertexOffset uint32   -> vertexCount * Vec3 (3 x float32)
//   0x50  faceCount uint32
//   0x54  faceOffset uint32     -> faceCount * 3 x uint32 vertex indices
//   0x58  faceTypeOffset uint32 -> faceCount * uint32 surface-material id
// ----------------------------------------------------------------------
internal static class BwmFile
{
    public const string Magic = "BWM V1.0";
    public const int MinLength = 0x88;

    public const int OffType           = 0x08;
    public const int OffVertexCount    = 0x48;
    public const int OffVertexOffset   = 0x4C;
    public const int OffFaceCount      = 0x50;
    public const int OffFaceOffset     = 0x54;
    public const int OffFaceTypeOffset = 0x58;

    // Surface-material ids that count as walkable floor. From
    // surfacemat.2da; the non-walkable remainder is walls, obstacles and
    // the various "non-walk" markers.
    public static readonly HashSet<int> WalkableTypes = new()
    {
        1,  // Dirt
        3,  // Grass
        4,  // Stone
        5,  // Wood
        6,  // Water
        9,  // Carpet
        10, // Metal
        11, // Puddles
        12, // Swamp
        13, // Mud
        14, // Leaves
        18, // Door
        21, // Trigger
        22, 23, 24, 25, 26, 27, 28, 29, 30,
    };

    public static uint ReadU32(byte[] b, int off) => BitConverter.ToUInt32(b, off);
    public static float ReadF32(byte[] b, int off) => BitConverter.ToSingle(b, off);

    public static bool HasValidHeader(byte[] bytes) =>
        bytes.Length >= MinLength &&
        System.Text.Encoding.ASCII.GetString(bytes, 0, 8) == Magic;
}
