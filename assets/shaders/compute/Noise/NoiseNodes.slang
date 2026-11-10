// The composable nodes of the noise graph: pointwise value nodes (value edges) and domain nodes (domain edges). Each
// node is a generic struct over its input fields, so a fused graph is one type expression. Each input field member
// has the name of its input slot, so a reflected parameter path (for example "control.source.frequency") maps back to
// the node and the slot.
//
// Value convention: bases and fractals return about [-1, 1]. Blend and Select read their control as it is; map a
// [-1, 1] field to [0, 1] with ScaleBias(0.5, 0.5) first.
module NoiseNodes;
__exported import NoiseField;

/// The bound of a node whose output has no useful upper bound, such as Power with a negative exponent.
public static const float k_UnknownBound = 3.0e38;

/// The largest control point count of Curve and Terrace.
public static const int k_MaxControlPoints = 16;

__include NodesValue;
__include NodesDomain;
