"""Normalize pose backend keypoints to the project's COCO-17 shape."""

import math


def _read_point(point):
    if isinstance(point, dict):
        x = point.get("x")
        y = point.get("y")
        score = point.get("score", point.get("confidence", point.get("v", 0.0)))
    else:
        try:
            x, y = point[0], point[1]
            score = point[2] if len(point) > 2 else 1.0
        except (IndexError, TypeError):
            return None
    try:
        values = [float(x), float(y), float(score)]
    except (TypeError, ValueError):
        return None
    if not all(math.isfinite(value) for value in values):
        return None
    return values


def _invalid_points():
    return [[0.0, 0.0, 0.0] for _ in range(17)]


def normalize_keypoints(points, image_width, image_height):
    """Return exactly 17 pixel-space ``[x, y, score]`` points.

    MMPose commonly returns pixel coordinates while other backends return
    normalized coordinates. The whole detection is classified as normalized
    only when every coordinate is within the normalized range.
    """
    try:
        width = float(image_width)
        height = float(image_height)
        raw = list(points)
    except (TypeError, ValueError):
        return _invalid_points()

    if width <= 0.0 or height <= 0.0 or len(raw) != 17:
        return _invalid_points()

    parsed = [_read_point(point) for point in raw]
    if any(point is None for point in parsed):
        return _invalid_points()

    normalized = all(
        -1.0 <= point[0] <= 1.0 and -1.0 <= point[1] <= 1.0
        for point in parsed
    )
    if normalized:
        return [[point[0] * width, point[1] * height, point[2]] for point in parsed]
    return parsed
