"""
K230 人体关键点推理与舞蹈机器人视觉动作识别。

模型推理部分沿用客户资料中的 ``person_keypoint_detect.py``：
``yolov8n-pose.kmodel`` 输出 COCO 17 点人体骨架，再由本文件的纯几何
分类器识别六种静态动作。分类器不依赖 CanMV，便于在 PC
上用合成关键点测试；K230 运行时才导入 ``libs``、``nncase_runtime``
和 ``aidemo``。
"""

import math

# ---------------------------------------------------------------------------
# 姿态类别常量
# ---------------------------------------------------------------------------
POSE_UNKNOWN = 0        # 未知/未识别动作
POSE_BIG_STAND = 1      # 大字站
POSE_BOW_STEP = 2       # 弓箭步
POSE_DOUBLE_HANDS = 3   # 举双手
POSE_SQUAT = 4          # 蹲下
POSE_HANDS_ON_HIPS = 11  # 叉腰
POSE_SALUTE = 12         # 敬礼

# 英文标签，用于日志或外部输出
POSE_LABELS = {
    POSE_UNKNOWN: "unknown",
    POSE_BIG_STAND: "big_stand",
    POSE_BOW_STEP: "bow_step",
    POSE_DOUBLE_HANDS: "double_hands",
    POSE_SQUAT: "squat",
    POSE_HANDS_ON_HIPS: "hands_on_hips",
    POSE_SALUTE: "salute",
}

# 中文标签，便于界面显示或调试输出
POSE_LABELS_CN = {
    POSE_UNKNOWN: "未识别",
    POSE_BIG_STAND: "大字站",
    POSE_BOW_STEP: "弓箭步",
    POSE_DOUBLE_HANDS: "举双手",
    POSE_SQUAT: "蹲下",
    POSE_HANDS_ON_HIPS: "叉腰",
    POSE_SALUTE: "敬礼",
}

# ---------------------------------------------------------------------------
# COCO 17 关键点索引
# yolov8n-pose.kmodel 的输出顺序与 COCO 定义一致。
# 这里只列出本文件实际使用的关键点索引，其余索引（如眼睛、耳朵）暂不使用。
# ---------------------------------------------------------------------------
NOSE = 0                # 鼻子
LEFT_SHOULDER = 5       # 左肩
RIGHT_SHOULDER = 6      # 右肩
LEFT_ELBOW = 7          # 左肘
RIGHT_ELBOW = 8         # 右肘
LEFT_WRIST = 9          # 左腕
RIGHT_WRIST = 10        # 右腕
LEFT_HIP = 11           # 左髋
RIGHT_HIP = 12          # 右髋
LEFT_KNEE = 13          # 左膝
RIGHT_KNEE = 14         # 右膝
LEFT_ANKLE = 15         # 左踝
RIGHT_ANKLE = 16        # 右踝


def _point(kps, index):
    """
    从关键点序列中取出指定索引的关键点。

    返回 ``(x, y, score)`` 三元组，并强制转换为 float。
    这样做可以避免 K230/CanMV 环境中 np.uint8、np.float16 等标量类型
    在后续算术运算中产生类型问题。
    """
    value = kps[index]
    return float(value[0]), float(value[1]), float(value[2])


def _distance(a, b):
    """计算两个二维点之间的欧氏距离，输入点只需包含 x, y。"""
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2)


def _angle(a, b, c):
    """
    计算以 b 为顶点、由 a-b-c 构成的夹角 ABC，单位为度。

    如果 a-b 或 b-c 的长度接近 0（即 b 与 a 或 c 重叠），
    则该角度无法定义，直接返回 180.0，表示该关节接近直线。
    """
    bax = a[0] - b[0]
    bay = a[1] - b[1]
    bcx = c[0] - b[0]
    bcy = c[1] - b[1]
    first = math.sqrt(bax * bax + bay * bay)
    second = math.sqrt(bcx * bcx + bcy * bcy)
    if first < 1e-6 or second < 1e-6:
        return 180.0
    cosine = (bax * bcx + bay * bcy) / (first * second)
    # 余弦值可能因浮点误差略超 [-1, 1]，先截断避免 acos 报错
    cosine = max(-1.0, min(1.0, cosine))
    return math.degrees(math.acos(cosine))


def _visible(kps, indices, threshold=0.25):
    """
    判断所有指定关键点的置信度是否都达到阈值。

    默认阈值 0.25 与 yolo 姿态模型的常用过滤值一致，
    可有效避免低置信度抖动点对几何分类造成干扰。
    """
    return all(_point(kps, i)[2] >= threshold for i in indices)


def make_roi(bbox, source_size, margin=0.35):
    """Return an expanded, in-bounds ``(x, y, width, height)`` ROI.

    ``bbox`` uses full-frame ``[left, top, right, bottom]`` coordinates.
    The returned rectangle is suitable for AI2D ``crop`` and always remains
    inside ``source_size``.  Invalid or degenerate boxes return ``None`` so
    the caller can safely fall back to a full-frame inference.
    """
    if bbox is None or len(bbox) < 4 or source_size is None or len(source_size) < 2:
        return None

    source_width = int(source_size[0])
    source_height = int(source_size[1])
    if source_width <= 0 or source_height <= 0:
        return None

    left, top, right, bottom = [float(value) for value in bbox[:4]]
    box_width = right - left
    box_height = bottom - top
    if box_width <= 0.0 or box_height <= 0.0:
        return None

    expand_x = box_width * max(0.0, float(margin))
    expand_y = box_height * max(0.0, float(margin))
    roi_left = max(0, int(math.floor(left - expand_x)))
    roi_top = max(0, int(math.floor(top - expand_y)))
    roi_right = min(source_width, int(math.ceil(right + expand_x)))
    roi_bottom = min(source_height, int(math.ceil(bottom + expand_y)))
    if roi_right <= roi_left or roi_bottom <= roi_top:
        return None
    return roi_left, roi_top, roi_right - roi_left, roi_bottom - roi_top


def map_roi_detections(boxes, keypoints, roi):
    """Map AI2D crop-local boxes/keypoints back into full-frame coordinates."""
    if roi is None:
        return boxes, keypoints

    offset_x = float(roi[0])
    offset_y = float(roi[1])
    mapped_boxes = []
    mapped_keypoints = []

    for box in boxes:
        mapped_box = [float(value) for value in box]
        if len(mapped_box) >= 4:
            mapped_box[0] += offset_x
            mapped_box[1] += offset_y
            mapped_box[2] += offset_x
            mapped_box[3] += offset_y
        mapped_boxes.append(mapped_box)

    for person_keypoints in keypoints:
        mapped_person = []
        for point in person_keypoints:
            mapped_point = [float(value) for value in point]
            if len(mapped_point) >= 2:
                mapped_point[0] += offset_x
                mapped_point[1] += offset_y
            mapped_person.append(mapped_point)
        mapped_keypoints.append(mapped_person)
    return mapped_boxes, mapped_keypoints


class PoseClassifier:
    """
    基于几何规则的人体姿态分类器，并带有简单的帧间去抖逻辑。

    该分类器不依赖 CanMV/nncase 等平台库，可直接在 PC 上使用
    合成关键点数据进行单元测试。
    """

    def __init__(self, score_threshold=0.25, stable_frames=4, history_size=6,
                 allowed_pose_ids=None):
        # 关键点置信度阈值，低于该值的点会被视为不可靠
        self.score_threshold = float(score_threshold)
        # 连续多少帧保持同一姿态才认为是“稳定姿态”
        self.stable_frames = max(1, int(stable_frames))
        # 历史记录最大长度，用于去抖判断
        self.history_size = max(self.stable_frames, int(history_size))
        # 保存最近若干帧的 pose_id
        self.history = []
        # 经过去抖后的稳定姿态 ID
        self.stable_id = POSE_UNKNOWN
        self.allowed_pose_ids = (
            None if allowed_pose_ids is None else set(allowed_pose_ids)
        )

    def reset(self):
        """Clear debounce state before processing a new host command."""
        self.history = []
        self.stable_id = POSE_UNKNOWN

    def _features(self, kps):
        """
        提取后续分类需要用到的几何特征。

        返回：
            points: 转换后的 17 个关键点列表，每个元素为 (x, y, score)
            scale:  归一化尺度，取肩宽、髋宽、躯干长度中的最大值
            shoulder_y: 左右肩 Y 坐标的平均值
            hip_y:     左右髋 Y 坐标的平均值

        使用 scale 可以让人体动作判断对不同大小、不同摄像头距离
        具有更好的鲁棒性。
        """
        points = [_point(kps, i) for i in range(17)]
        shoulder_width = _distance(points[LEFT_SHOULDER], points[RIGHT_SHOULDER])
        hip_width = _distance(points[LEFT_HIP], points[RIGHT_HIP])
        # 躯干长度：肩中点到髋中点的距离
        torso = _distance(
            ((points[LEFT_SHOULDER][0] + points[RIGHT_SHOULDER][0]) / 2.0,
             (points[LEFT_SHOULDER][1] + points[RIGHT_SHOULDER][1]) / 2.0),
            ((points[LEFT_HIP][0] + points[RIGHT_HIP][0]) / 2.0,
             (points[LEFT_HIP][1] + points[RIGHT_HIP][1]) / 2.0),
        )
        # 避免 scale 为 0
        scale = max(shoulder_width, hip_width, torso, 1.0)
        shoulder_y = (points[LEFT_SHOULDER][1] + points[RIGHT_SHOULDER][1]) / 2.0
        hip_y = (points[LEFT_HIP][1] + points[RIGHT_HIP][1]) / 2.0
        return points, scale, shoulder_y, hip_y

    def classify(self, kps):
        """Classify one frame and apply the optional team action allow-list."""
        pose_id, confidence = self._classify_unfiltered(kps)
        if (
                self.allowed_pose_ids is not None and
                pose_id != POSE_UNKNOWN and
                pose_id not in self.allowed_pose_ids
        ):
            return POSE_UNKNOWN, 0.0
        return pose_id, confidence

    def _classify_unfiltered(self, kps):
        """
        对单帧 COCO 17 点骨架进行分类。

        返回：
            (pose_id, confidence)
            pose_id    为 POSE_* 常量之一
            confidence 为 0~0.99 之间的浮点置信度

        若关键点数量不足、关键点不可见或最终得分过低，
        返回 (POSE_UNKNOWN, confidence)。
        """
        if kps is None or len(kps) < 17:
            return POSE_UNKNOWN, 0.0

        # 允许姿态模型有少量关键点抖动；人体框本身已经由后端过滤。
        visible_threshold = max(0.15, self.score_threshold - 0.05)
        body_required = [
            LEFT_SHOULDER, RIGHT_SHOULDER, LEFT_HIP, RIGHT_HIP,
            LEFT_KNEE, RIGHT_KNEE, LEFT_ANKLE, RIGHT_ANKLE,
        ]
        if not _visible(kps, body_required, visible_threshold):
            return POSE_UNKNOWN, 0.0

        arms_reliable = _visible(
            kps,
            [LEFT_SHOULDER, RIGHT_SHOULDER, LEFT_ELBOW, RIGHT_ELBOW,
             LEFT_WRIST, RIGHT_WRIST],
            visible_threshold,
        )

        # 提取归一化后的特征与关键点
        points, scale, shoulder_y, hip_y = self._features(kps)

        # 计算左右膝盖弯曲角（髋-膝-踝）
        left_knee = _angle(points[LEFT_HIP], points[LEFT_KNEE], points[LEFT_ANKLE])
        right_knee = _angle(points[RIGHT_HIP], points[RIGHT_KNEE], points[RIGHT_ANKLE])
        left_arm_angle = _angle(
            points[LEFT_SHOULDER], points[LEFT_ELBOW], points[LEFT_WRIST]
        )
        right_arm_angle = _angle(
            points[RIGHT_SHOULDER], points[RIGHT_ELBOW], points[RIGHT_WRIST]
        )

        nose_x = points[NOSE][0]
        nose_y = points[NOSE][1]
        leg_spread = abs(points[LEFT_ANKLE][0] - points[RIGHT_ANKLE][0]) / scale
        ankle_height_gap = abs(
            points[LEFT_ANKLE][1] - points[RIGHT_ANKLE][1]
        ) / scale
        feet_side_by_side = ankle_height_gap < 0.35
        knees_straight = left_knee > 150.0 and right_knee > 150.0

        # 敬礼、叉腰和弓箭步优先于四个基础动作。每项均使用相对人体尺度，
        # 因此人远近变化时阈值仍然有效。
        left_salute = (
            arms_reliable and
            left_arm_angle < 165.0 and
            points[LEFT_WRIST][1] < nose_y + 0.55 * scale and
            abs(points[LEFT_WRIST][0] - nose_x) < 0.45 * scale
        )
        right_salute = (
            arms_reliable and
            right_arm_angle < 165.0 and
            points[RIGHT_WRIST][1] < nose_y + 0.55 * scale and
            abs(points[RIGHT_WRIST][0] - nose_x) < 0.45 * scale
        )
        salute = (
            (left_salute and points[RIGHT_WRIST][1] > shoulder_y + 0.35 * scale) or
            (right_salute and points[LEFT_WRIST][1] > shoulder_y + 0.35 * scale)
        )
        one_leg_bent = (
            min(left_knee, right_knee) < 165.0 and
            max(left_knee, right_knee) > 135.0 and
            abs(left_knee - right_knee) > 8.0
        )
        ankle_distance = _distance(
            points[LEFT_ANKLE], points[RIGHT_ANKLE]
        ) / scale
        front_back_stance = max(leg_spread, ankle_height_gap, ankle_distance) > 0.28
        bow_step = one_leg_bent and front_back_stance
        wrist_near_hip = (
            min(
                _distance(points[LEFT_WRIST], points[LEFT_HIP]),
                _distance(points[LEFT_WRIST], points[RIGHT_HIP]),
            ) < 0.85 * scale and
            min(
                _distance(points[RIGHT_WRIST], points[LEFT_HIP]),
                _distance(points[RIGHT_WRIST], points[RIGHT_HIP]),
            ) < 0.85 * scale
        )
        elbows_out = (
            abs(points[LEFT_ELBOW][0] - points[LEFT_SHOULDER][0]) > 0.06 * scale and
            abs(points[RIGHT_ELBOW][0] - points[RIGHT_SHOULDER][0]) > 0.06 * scale
        )
        hands_on_hips = (
            arms_reliable and
            points[LEFT_WRIST][1] > shoulder_y + 0.10 * scale and
            points[RIGHT_WRIST][1] > shoulder_y + 0.10 * scale and
            points[LEFT_WRIST][1] < hip_y + 0.95 * scale and
            points[RIGHT_WRIST][1] < hip_y + 0.95 * scale and
            wrist_near_hip and
            elbows_out
        )
        arm_horizontal = arms_reliable and (
            abs(points[LEFT_WRIST][1] - points[LEFT_SHOULDER][1]) < 0.40 * scale and
            abs(points[RIGHT_WRIST][1] - points[RIGHT_SHOULDER][1]) < 0.40 * scale
        )

        if bow_step:
            return POSE_BOW_STEP, 0.90
        if salute:
            return POSE_SALUTE, 0.92
        if hands_on_hips:
            return POSE_HANDS_ON_HIPS, 0.92

        # 四个基础动作使用分数，并加入互斥的姿态几何条件。
        scores = {
            POSE_BIG_STAND: 0.0,
            POSE_BOW_STEP: 0.0,
            POSE_DOUBLE_HANDS: 0.0,
            POSE_SQUAT: 0.0,
        }

        # 举双手要求双腕都高过头部、双肘都高于肩部；单手举起不能通过。
        wrists_high = (
            points[LEFT_WRIST][1] < nose_y + 0.15 * scale and
            points[RIGHT_WRIST][1] < nose_y + 0.15 * scale
        )
        elbows_high = (
            points[LEFT_ELBOW][1] < shoulder_y + 0.25 * scale and
            points[RIGHT_ELBOW][1] < shoulder_y + 0.25 * scale
        )
        if arms_reliable and wrists_high and elbows_high:
            scores[POSE_DOUBLE_HANDS] = 0.95

        # 大字站必须双臂水平、双腿伸直、双脚左右分开且脚底高度接近。
        if leg_spread > 0.75 and arm_horizontal and knees_straight and feet_side_by_side:
            scores[POSE_BIG_STAND] = 0.95

        # 蹲下要求双膝都弯曲并且髋部明显下降；单腿前后站姿不算蹲下。
        both_knees_bent = left_knee < 150.0 and right_knee < 150.0
        hips_low = hip_y - shoulder_y > 0.75 * scale
        if both_knees_bent and hips_low and leg_spread < 1.35:
            scores[POSE_SQUAT] = 0.90

        # 弓箭步左右镜像等价：一膝弯曲、另一膝伸直，双脚明显分开。
        # 侧视时两脚通常都在地面上，不能再用踝关节高度差作为必要条件。
        if one_leg_bent and front_back_stance:
            scores[POSE_BOW_STEP] = 0.90

        # 取得分最高的动作作为本帧分类结果
        pose_id = max(scores, key=scores.get)
        confidence = min(0.99, max(0.0, scores[pose_id]))
        # 置信度过低时返回未知，防止将噪声误判为有效动作
        if confidence < 0.50:
            return POSE_UNKNOWN, confidence
        return pose_id, confidence

    def update(self, kps):
        """
        对当前帧关键点进行分类，并执行简单的帧间去抖。

        返回字典包含：
            pose_id          当前帧动作 ID
            pose             当前帧英文标签
            pose_cn          当前帧中文标签
            confidence       当前帧置信度
            stable           当前动作是否与稳定动作一致且非未知
            stable_pose_id   稳定动作 ID
            stable_pose      稳定动作英文标签
            stable_pose_cn   稳定动作中文标签
        """
        pose_id, confidence = self.classify(kps)

        # 更新历史记录，仅保留最近 history_size 帧
        self.history.append(pose_id)
        if len(self.history) > self.history_size:
            self.history = self.history[-self.history_size:]

        # 只有最近 stable_frames 帧均为当前动作，当前帧才是稳定的。
        # 不能仅比较 ``stable_id``，否则中途出现 unknown 后又恢复同一
        # 动作时，会错误地把第一帧恢复画面当作稳定结果。
        recent = self.history[-self.stable_frames:]
        stable_now = (
            pose_id != POSE_UNKNOWN and
            len(recent) == self.stable_frames and
            all(item == pose_id for item in recent)
        )
        if stable_now:
            self.stable_id = pose_id

        return {
            "pose_id": pose_id,
            "pose": POSE_LABELS[pose_id],
            "pose_cn": POSE_LABELS_CN[pose_id],
            "confidence": round(confidence, 3),
            "stable": stable_now,
            "stable_pose_id": self.stable_id,
            "stable_pose": POSE_LABELS[self.stable_id],
            "stable_pose_cn": POSE_LABELS_CN[self.stable_id],
        }


def empty_pose_result():
    """
    返回一个空的姿态识别结果字典。

    该函数保证在没有检测到人体或识别失败时，上层接口仍然能够
    获得格式一致的结果，避免调用方做额外的空值判断。
    """
    return {
        "task": "pose",
        "count": 0,
        "pose_id": POSE_UNKNOWN,
        "pose": POSE_LABELS[POSE_UNKNOWN],
        "pose_cn": POSE_LABELS_CN[POSE_UNKNOWN],
        "confidence": 0.0,
        "stable": False,
        "stable_pose_id": POSE_UNKNOWN,
        "stable_pose": POSE_LABELS[POSE_UNKNOWN],
        "stable_pose_cn": POSE_LABELS_CN[POSE_UNKNOWN],
        "items": [],
    }


# 首帧以及每隔 5 帧使用一次完整画面，避免目标位置变化或 ROI 跟丢后
# 长时间只观察到错误区域。中间 4 帧放大上一帧人体附近区域以改善关键点质量。
POSE_ROI_MARGIN = 0.35
POSE_ROI_FULL_FRAME_INTERVAL = 3


class K230PoseRecognizer:
    """
    CanMV/K230 平台上的姿态识别适配器。

    封装客户提供的 YOLOv8 姿态检测模型（yolov8n-pose.kmodel），
    负责模型加载、预处理、后处理，并调用 PoseClassifier 完成动作分类。
    """

    def __init__(self, kmodel_path="/sdcard/app/tests/kmodel/yolov8n-pose.kmodel",
                 rgb888p_size=(1280, 720), model_input_size=(320, 320),
                 confidence_threshold=0.20, nms_threshold=0.50, debug_mode=0,
                 allowed_pose_ids=None):
        """
        初始化 K230 姿态识别器。

        参数：
            kmodel_path          模型文件路径
            rgb888p_size         摄像头输入分辨率 (宽, 高)
            model_input_size     模型输入尺寸 (宽, 高)，通常为 320x320
            confidence_threshold 模型后处理置信度阈值
            nms_threshold        非极大值抑制阈值
            debug_mode           调试模式，0 关闭，1 打开
        """
        # 将平台相关库的导入放在函数内部，这样在 PC 上使用 PoseClassifier
        # 做纯几何测试时不会因为这些库缺失而无法导入本模块。
        from libs.AI2D import Ai2d
        from libs.AIBase import AIBase
        import aidemo
        import nncase_runtime as nn
        import ulab.numpy as np
        from media.media import ALIGN_UP

        class _PersonKeyPointApp(AIBase):
            """AIBase 派生类，负责模型加载与前后处理配置。"""

            def __init__(self, path, input_size, confidence, nms, source_size, debug):
                # 调用父类 AIBase 的初始化方法，加载 kmodel 并配置基本参数
                super().__init__(path, input_size, source_size, debug)
                self.model_input_size = list(input_size)
                self.confidence_threshold = confidence
                self.nms_threshold = nms
                # 将输入宽度对齐到 16 字节，满足 AI2D 硬件要求
                self.rgb888p_size = [ALIGN_UP(source_size[0], 16), source_size[1]]
                self._postprocess_size = list(self.rgb888p_size)
                self.debug = debug
                self._create_ai2d()

            def _create_ai2d(self):
                """Create a fresh AI2D graph before changing the crop region."""
                self.ai2d = Ai2d(self.debug)
                self.ai2d.set_ai2d_dtype(
                    nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                    np.uint8, np.uint8,
                )

            def config_preprocess(self, crop=None):
                """
                配置预处理流水线：可选 crop -> padding -> resize。

                ``crop`` 为原始相机坐标中的 ``(x, y, width, height)``。
                即使裁剪，AI2D 图的输入仍然是完整 RGBP 帧；crop 操作会在
                硬件预处理链的第一步取出目标区域。后处理尺寸同步改为 ROI
                尺寸，使输出的框和关键点先保持 ROI 局部坐标。
                """
                self._create_ai2d()
                input_size = self.rgb888p_size
                roi_size = input_size
                if crop is not None:
                    crop_x, crop_y, crop_width, crop_height = [int(value) for value in crop]
                    if crop_width <= 0 or crop_height <= 0:
                        raise ValueError("pose ROI size must be positive")
                    self.ai2d.crop(crop_x, crop_y, crop_width, crop_height)
                    roi_size = [crop_width, crop_height]

                self._postprocess_size = list(roi_size)
                top, bottom, left, right = self._padding(roi_size)
                # pad 参数格式通常为 [上, 下, 左, 右]，此处使用 0 填充
                self.ai2d.pad([0, 0, 0, 0, top, bottom, left, right], 0, [0, 0, 0])
                # 使用双线性插值进行缩放
                self.ai2d.resize(nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)
                # 构建预处理图，输入形状为 NCHW，输出形状为模型输入
                self.ai2d.build(
                    [1, 3, input_size[1], input_size[0]],
                    [1, 3, self.model_input_size[1], self.model_input_size[0]],
                )

            def _padding(self, input_size):
                """
                计算等比例缩放后所需的上下左右 padding 像素数。

                返回顺序为 (top, bottom, left, right)。
                该方法保证缩放后的图像在模型输入尺寸中居中。
                """
                dst_w, dst_h = self.model_input_size
                input_w, input_h = input_size
                ratio = min(float(dst_w) / input_w, float(dst_h) / input_h)
                new_w, new_h = int(ratio * input_w), int(ratio * input_h)
                dw, dh = (dst_w - new_w) / 2.0, (dst_h - new_h) / 2.0
                # 上下 padding 采用不同的小数舍入，用于平衡奇偶像素差
                return (int(round(dh - 0.1)), int(round(dh + 0.1)),
                        int(round(dw - 0.1)), int(round(dw - 0.1)))

            def postprocess(self, results):
                """
                模型输出后处理，调用 aidemo 提供的人员关键点后处理函数。

                返回结果通常为 (boxes, keypoints) 形式：
                    boxes:      检测到的人体框，形状为 (N, 4)
                    keypoints:  每个人体的 17 个关键点，形状为 (N, 17, 3)
                """
                return aidemo.person_kp_postprocess(
                    results[0], [self._postprocess_size[1], self._postprocess_size[0]],
                    self.model_input_size, self.confidence_threshold,
                    self.nms_threshold,
                )

        # 实例化人体关键点检测模型，并完成预处理配置
        self.model = _PersonKeyPointApp(
            kmodel_path, list(model_input_size), confidence_threshold,
            nms_threshold, list(rgb888p_size), debug_mode,
        )
        self.model.config_preprocess()
        # 创建几何规则分类器，用于识别六种静态动作
        self.classifier = PoseClassifier(allowed_pose_ids=allowed_pose_ids)
        self.source_size = (int(rgb888p_size[0]), int(rgb888p_size[1]))
        self.roi = None
        self.roi_frames_since_full = 0
        self._configured_roi = None
        self._preprocess_configured = True

    def process(self, frame):
        """
        对一帧图像执行姿态检测与动作分类。

        参数：
            frame: 摄像头采集的 RGB888P 图像数据

        返回：
            姿态识别结果字典，包含动作分类、稳定状态、人体框等信息。
        """
        # 默认返回空结果，保证输出格式统一
        result = empty_pose_result()
        source_size = getattr(self, "source_size", (1280, 720))
        roi = getattr(self, "roi", None)
        roi_frames_since_full = getattr(self, "roi_frames_since_full", 0)
        use_roi = (
            roi is not None and
            roi_frames_since_full < POSE_ROI_FULL_FRAME_INTERVAL - 1
        )
        active_roi = roi if use_roi else None

        # 只有全图/ROI 模式切换时才重建 AI2D，避免每个 ROI 帧重复创建图。
        config_preprocess = getattr(self.model, "config_preprocess", None)
        configured_roi = getattr(self, "_configured_roi", None)
        preprocess_configured = getattr(self, "_preprocess_configured", False)
        if config_preprocess is not None and (
                not preprocess_configured or active_roi != configured_roi):
            config_preprocess(active_roi)
            self._configured_roi = active_roi
            self._preprocess_configured = True
        detections = self.model.run(frame)

        # detections 为空或未检测到人体时直接返回空结果，并清空防抖历史。
        # 人物重新进入画面后必须重新积累连续帧，不能沿用离开前的稳定动作。
        if not detections or len(detections[0]) == 0:
            self.classifier.reset()
            # ROI 内没有人时不在本帧再次推理，下一帧直接使用完整画面恢复。
            self.roi = None
            self.roi_frames_since_full = 0
            return result

        # 假设 detections 结构为 (boxes, keypoints)
        boxes = detections[0]
        keypoints = detections[1]
        if active_roi is not None:
            boxes, keypoints = map_roi_detections(boxes, keypoints, active_roi)

        # 比赛场景中通常只有一名选手，但若画面中出现多人，
        # 则选择人体框面积最大的作为目标，避免背景人物干扰。
        best = 0
        best_area = -1.0
        for index, box in enumerate(boxes):
            area = max(0.0, float(box[2] - box[0])) * max(0.0, float(box[3] - box[1]))
            if area > best_area:
                best, best_area = index, area

        # 对选中的人体关键点进行动作分类和去抖
        classified = self.classifier.update(keypoints[best])

        # 在分类结果中补充任务信息、检测人数和人体框
        bbox = [round(float(value), 1) for value in boxes[best][:4]]
        classified.update({
            "task": "pose",
            "count": len(boxes),
            "bbox": bbox,
            "source_size": source_size,
            # 保留原始相机坐标系下的关键点，供 LCD OSD 绘制骨架使用。
            # 转成 Python float/list，避免 ulab 数组在跨模块使用时失效。
            "keypoints": [
                [round(float(point[0]), 1), round(float(point[1]), 1),
                 round(float(point[2]), 3)]
                for point in keypoints[best]
            ],
            "items": [{
                "pose": classified["pose"],
                "pose_cn": classified["pose_cn"],
                "confidence": classified["confidence"],
            }],
        })
        if active_roi is None:
            # 只用全图检测结果更新跟踪区域；ROI 帧复用当前区域，避免每帧
            # 改变 crop 参数并重建 AI2D，同时保留周期性全图校正。
            self.roi = make_roi(bbox, source_size, POSE_ROI_MARGIN)
            self.roi_frames_since_full = 0
        else:
            self.roi_frames_since_full = roi_frames_since_full + 1
        return classified

    def reset(self):
        """Reset frame debounce before a new host-triggered recognition."""
        self.classifier.reset()
        self.roi = None
        self.roi_frames_since_full = 0
        self._configured_roi = None
        self._preprocess_configured = False

    def deinit(self):
        """
        释放模型及相关资源。

        调用 AIBase 的 deinit 方法完成 kmodel 和内存的清理，
        避免在连续运行或多次初始化时出现资源泄漏。
        """
        deinit = getattr(self.model, "deinit", None)
        if deinit is not None:
            deinit()
