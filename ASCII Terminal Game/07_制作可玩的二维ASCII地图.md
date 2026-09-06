# 07 制作可玩的二维 ASCII 地图

## 先观察现成 Demo

选择 `GameObject > ASCII Terminal > Create 2D Map Demo`，进入 Play Mode：

- WASD/方向键移动；
- E 与相邻物体交互；
- V 切换局部视野与完整地图；
- R 重置。

先回答：地图数据、玩家位置、已探索区域、窗口布局，哪些属于游戏状态，哪些只是显示？

## 最小地图模型

```csharp
private readonly string[] _map =
{
    "####################",
    "#........#.........#",
    "#..S.....+....E....#",
    "#........#.........#",
    "####################"
};

private Vector2Int _player = new Vector2Int(2, 2);
```

建议约定：

| 字符 | 语义 |
|---|---|
| `#` | 墙 |
| `.` | 地面 |
| `+` | 关闭的门 |
| `/` | 打开的门 |
| `S` | 信号碎片 |
| `E` | 出口 |
| `@` | 玩家（绘制覆盖，不写回地图） |

## 判断可走

```csharp
private bool IsWalkable(Vector2Int p)
{
    if (p.y < 0 || p.y >= _map.Length)
        return false;
    if (p.x < 0 || p.x >= _map[p.y].Length)
        return false;

    char tile = _map[p.y][p.x];
    return tile != '#' && tile != '+';
}
```

这里只是教学用不可变字符串地图。正式项目如果门和物品会改变，应改为 `char[,]`、Tile 数据对象或独立实体表。

## 绘制地图

```csharp
private static readonly TerminalStyle WallStyle = new TerminalStyle(
    new Color32(130, 145, 145, 255), new Color32(0, 0, 0, 255));

private static readonly TerminalStyle FloorStyle = new TerminalStyle(
    new Color32(48, 68, 72, 255), new Color32(0, 0, 0, 255));

private static readonly TerminalStyle PlayerStyle = new TerminalStyle(
    new Color32(240, 255, 225, 255), new Color32(0, 0, 0, 255));

private void DrawMap()
{
    TerminalCanvas c = _mapWindow.Canvas;
    c.Clear(FloorStyle);

    for (int y = 0; y < _map.Length; y++)
    {
        for (int x = 0; x < _map[y].Length; x++)
        {
            char tile = _map[y][x];
            c.Put(x, y, tile, tile == '#' ? WallStyle : FloorStyle);
        }
    }

    c.Put(_player.x, _player.y, '@', PlayerStyle);
}
```

## 摄像机/视口式地图

地图大于 Surface 时，不要把世界坐标直接当 Canvas 坐标。计算视口左上角：

```csharp
Vector2Int viewOrigin = new Vector2Int(
    _player.x - _mapWindow.Canvas.Width / 2,
    _player.y - _mapWindow.Canvas.Height / 2);

Vector2Int screen = world - viewOrigin;
```

遍历屏幕格时反向求世界位置通常更简单：

```csharp
for (int sy = 0; sy < canvas.Height; sy++)
for (int sx = 0; sx < canvas.Width; sx++)
{
    Vector2Int world = viewOrigin + new Vector2Int(sx, sy);
    // 查询 world，再画到 sx, sy
}
```

## Fog of War

维护 `bool[,] seen`：

- 当前视野内：正常样式并把 `seen=true`；
- 已见但不在当前视野：暗色样式；
- 从未见过：画空格。

初版可以用方形/曼哈顿距离，之后再换射线可见性。先让状态和绘制分离。

## 三窗口自由布局

参考结构：顶部状态、主地图、日志面板。但它不是固定模板：

```csharp
_header = context.OpenWindow(
    "header", new Rect(0f, 0f, 1f, 0.08f),
    new Vector2Int(152, 4), 20);

_mapWindow = context.OpenWindow(
    "map", new Rect(0f, 0.08f, 0.7f, 0.92f),
    new Vector2Int(100, 64), 0);

_panel = context.OpenWindow(
    "panel", new Rect(0.7f, 0.08f, 0.3f, 0.92f),
    new Vector2Int(42, 64), 10);
```

剧情时可以让地图全屏、战斗时让状态框浮在中间、视频时隐藏全部 UI。窗口是舞台，不是固定网页布局。

## 性能策略

- 地图没变化时不必每帧重画；
- 玩家移动后，可以先简单全重画，确认正确再优化；
- 大地图只画视口；
- 粒子/闪烁效果交给效果层，不要为了一个闪烁物体重写全部 80,000 格；
- 使用 Surface 批写能力处理大量格子。

## 本章项目

制作一个 30×15 房间：

- 玩家可移动；
- 有一扇门和一个物品；
- E 打开门/拾取；
- 右侧面板显示 HP、物品和最近五条日志；
- V 开关迷雾；
- 分辨率变化后窗口仍正确。

## 验收

- 不能穿墙或穿关闭的门；
- 玩家字符不写进底层地图；
- 打开门后地图状态真正改变；
- UI 不是从屏幕字符反推游戏状态；
- Console 无越界错误；
- 能解释 world 坐标和 canvas 坐标的换算。

