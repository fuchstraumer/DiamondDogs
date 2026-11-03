
implementing Quadtree;

internal uint DeinterleaveWord(in uint x)
{
    x = (x | (x >> 1u)) & 0x33333333u;
    x = (x | (x >> 2u)) & 0x0F0F0F0Fu;
    x = (x | (x >> 4u)) & 0x00FF00FFu;
    x = (x | (x >> 8u)) & 0x0000FFFFu;
    return x;
}

// pos.xy = deinterleaved Morton code, pos.z = level
// first four bits store the level, leaving the other 28 for the for the code
// this gives us up to a 15 level quadree (including root)
internal uint DecodeQuadtreePosition(in uint key, out uint3 pos)
{
    pos.z = key & 0xFu;
    pos.x = DeinterleaveWord((key >> 4u) & 0x05555555u);
    pos.y = DeinterleaveWord((key >> 5u) & 0x05555555u);
    return key >> 4u;
}

internal void QuadtreeChildrenFromKey(in uint key, out uint4 child_nodes)
{
    key = (++key & 0xF) | ((key & ~0xF) << 2u);
    child_nodes.x = key;
    child_nodes.y = key | 0x10u;
    child_nodes.z = key | 0x20u;
    child_nodes.w = key | 0x30u;
}

internal uint QuadtreeParentFromKey(in uint key)
{
    return ((--key & 0xF) | ((key >> 2u) & 0x3FFFFFF0u));
}

// Only the upper left child writes data when merging back into the parent node
internal bool NodeIsUpperLeft(in uint key)
{
    return (key & 0x30u) == 0u;
}
