# 补充：C# 与 Unity 最小补课

只有遇到对应概念看不懂时再阅读，不要求预先完成。

## 必须会的 C#

- 类、字段、方法和构造函数；
- `public/private`；
- 数组、`List<T>`、`Dictionary<TKey,TValue>`；
- `interface` 与继承；
- `event` 的订阅和退订；
- `IDisposable` 与资源释放；
- 值类型 `struct` 与引用类型 `class` 的基本区别。

## 必须会的 Unity

- GameObject、Component、MonoBehaviour；
- Inspector 序列化字段；
- Scene、Prefab、Camera；
- Play Mode 与 Console；
- `Vector2Int`、`Rect`、`RectInt`；
- `Awake/OnEnable/Start/Update/OnDisable` 的基本时机。

## 最小理解

`TerminalModuleBehaviour` 仍然是 MonoBehaviour，但它的终端逻辑由
`TerminalModuleHost` 调用。不要用 `new HelloTerminalModule()`；把脚本作为组件添加到
GameObject。资源申请和释放必须成对，事件订阅与退订也必须成对。

若这些概念无法解释，先用一个空 Unity 项目分别做：计数器组件、按键移动组件、事件订阅组件，再回到第 2 章。
