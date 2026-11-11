# Zekit 样式字段清单（精简版 v1）

> 面向 C++ GUI 框架，对标 CSS 设计语言。原文的完整 CSS 属性清单已按「落地范围」筛过一轮：
> 能映射到绘制/排版/布局的留下，浏览器专属、GUI 无意义、后期再说的移出。
> 完整版对照表见下方【砍掉的属性】。
>
> 筛选标准与 `BoxStyle` / `TextStyle` / `FlexStyle` 三模块划分一致。

## 0. 筛选规则

| 判定 | 处理 |
| --- | --- |
| 能映射到**盒子模型**（盒子/边框/圆角/阴影/内边距） | ✅ 保留，进 `BoxStyle` |
| 能映射到**文字排版**（字体/对齐/行高/字距/阴影） | ✅ 保留，进 `TextStyle` |
| 能映射到**容器布局**（方向/对齐/换行/gap） | ✅ 保留，进 `FlexStyle` |
| 属于**几何层**（尺寸），不属于绘制阶段 | ⚠️ 留在 `Widget`，样式表不定义 |
| 浏览器/文档流专属（`float` `clear`、多列、`nav-*`、`unicode-bidi` 等） | ❌ 砍 |
| 后期功能（Grid、动画/过渡、3D） | ⏳ 缓，本版不进表 |

---

## 1. BoxStyle — 盒子模型

> 适用：所有可见矩形控件。对应 CSS 的 content / padding / border 与背景绘制。

### 1.1 背景

| 字段 | 对应 CSS | 说明 | 取舍 |
| --- | --- | --- | --- |
| `backgroundColor` | `background-color` | 背景颜色 | ✅ |
| `backgroundImage` | `background-image` | 背景图片 / 九宫格图 | ✅ |
| `backgroundSize` | `background-size` | 背景图尺寸（`cover` / `contain` / 具体值） | ✅ |
| `backgroundPosition` | `background-position` | 背景图起始位置 | ✅ |
| `backgroundRepeat` | `background-repeat` | 是否平铺 / 拉伸 | ✅ |
| `backgroundClip` | `background-clip` | 绘制区域（border-box / padding-box） | ⏳ 需要再说 |
| `backgroundOrigin` | `background-origin` | 定位区域基准 | ⏳ 需要再说 |
| `backgroundAttachment` | `background-attachment` | 背景是否随内容滚动 | ❌ 砍（GUI 里只有滚动容器，语义不对应） |

背景简写（`background`）**不单独设字段** —— C++ 里用结构体聚合天然就是简写，不需要额外一个 `background` 入口。

### 1.2 边框

| 字段 | 对应 CSS | 说明 | 取舍 |
| --- | --- | --- | --- |
| `borderWidth` | `border-width` | 四边宽度（可分别设上/右/下/左） | ✅ |
| `borderColor` | `border-color` | 四边颜色（可分别设） | ✅ |
| `borderStyle` | `border-style` | `none` / `solid` / `dashed` / `dotted` | ✅ 只留 GUI 画得出来的 |
| `borderRadius` | `border-radius` | 圆角（走 `RadiusMode` → 主题 token） | ✅ |
| `borderImage` | `border-image` | 边框图片 | ⏳ 缓（九宫格已有 image 方案，先不并行造第二个） |

`border` / `border-top` / `border-left` … 这些**简写形式不设字段** —— 在 C++ 里直接写 `box.borderWidth.top = 2`，用聚合代替简写。

`groove` / `ridge` / `inset` / `outset` / `double` 这五种立体边框样式 **砍** —— GUI 里用阴影和描边做质感，比假立体边框可靠。

### 1.3 轮廓（Outline）

| 字段 | 对应 CSS | 说明 | 取舍 |
| --- | --- | --- | --- |
| `outlineWidth` | `outline-width` | 外描边宽度 | ✅ |
| `outlineColor` | `outline-color` | 外描边颜色 | ✅ |
| `outlineStyle` | `outline-style` | 外描边样式 | ✅ |
| `outlineOffset` | `outline-offset` | 外描边外扩距离 | ✅ |

轮廓和边框的区别要保住：**边框占布局空间，轮廓不占**。焦点框用轮廓画，比在边框上叠一层更干净。

### 1.4 阴影与内边距

| 字段 | 对应 CSS | 说明 | 取舍 |
| --- | --- | --- | --- |
| `shadow` | `box-shadow` / `text-shadow` | 全库统一一个 `Shadow` 类型，多份阴影用数组 | ✅ |
| `padding` | `padding` | 内边距（上/右/下/左） | ✅ |
| `opacity` | `opacity` | 整体不透明度 | ✅ |
| `cursor` | `cursor` | 鼠标指针类型 | ✅ |

`margin` **砍出样式表** —— 外边距是容器布局的事，属于父容器的 `gap` / 对齐问题，不归子控件样式管。这一条和「尺寸留在 Widget」是同一个判断。

---

## 2. TextStyle — 文字排版

> 适用：所有带文字的控件。布局容器不继承这张表。

### 2.1 字体

| 字段 | 对应 CSS | 说明 | 取舍 |
| --- | --- | --- | --- |
| `font` | `font` | 整体字体描述（`FontDesc`） | ✅ |
| `fontFamily` | `font-family` | 字体系列 | ✅ |
| `fontSize` | `font-size` | 字号 | ✅ |
| `fontWeight` | `font-weight` | 字重 | ✅ |
| `fontStyle` | `font-style` | `normal` / `italic` | ✅ |
| `fontVariant` | `font-variant` | 小型大写 | ❌ 砍（中文字体无此需求） |
| `fontStretch` | `font-stretch` | 字体拉伸 | ❌ 砍（中文字体无此需求） |

### 2.2 排版与装饰

| 字段 | 对应 CSS | 说明 | 取舍 |
| --- | --- | --- | --- |
| `color` | `color` | 文字颜色 | ✅ |
| `hAlign` | `text-align` | 水平对齐 | ✅ |
| `vAlign` | `vertical-align` | 垂直对齐 | ✅ |
| `lineHeight` | `line-height` | 行高 | ✅ |
| `letterSpacing` | `letter-spacing` | 字符间距 | ✅ |
| `wordSpacing` | `word-spacing` | 字间距 | ⏳ 缓（与 `letterSpacing` 功能重叠，先只留一个） |
| `decoration` | `text-decoration` | 下划线 / 删除线 | ✅ |
| `textShadow` | `text-shadow` | 文字阴影 —— 合并进统一 `Shadow` | ✅ |
| `textIndent` | `text-indent` | 首行缩进 | ✅ |
| `textTransform` | `text-transform` | 大小写转换 | ⏳ 缓（英文场景才用） |
| `whiteSpace` | `white-space` | 空白处理 / 是否换行 | ✅ |
| `wordBreak` | `word-break` | 长单词断行规则 | ✅ |
| `lineClamp` | `text-overflow` | 溢出省略（`…`） | ✅ |
| `direction` | `direction` | 文本方向（LTR/RTL） | ❌ 砍（先只做 LTR） |
| `unicodeBidi` | `unicode-bidi` | 双向文本重写 | ❌ 砍 |
| `writingMode` | —— | 竖排 | ❌ 砍 |

> `hanging-punctuation` / `punctuation-trim` / `text-align-last` / `text-justify` / `text-emphasis` / `text-outline` / `text-wrap` —— **全部砍**，浏览器排版遗留物，GUI 里没有对应实现。

---

## 3. FlexStyle — 容器布局

> 适用：容器控件。这是整块新增，目前还没有布局容器。

| 字段 | 对应 CSS | 说明 | 取舍 |
| --- | --- | --- | --- |
| `direction` | `flex-direction` | 主轴方向（row / column / 反向） | ✅ |
| `justifyContent` | `justify-content` | 主轴对齐 | ✅ |
| `alignItems` | `align-items` | 交叉轴对齐 | ✅ |
| `alignSelf` | `align-self` | 子元素覆盖父容器交叉轴对齐 | ✅ |
| `alignContent` | `align-content` | 多行时的行对齐 | ✅ |
| `wrap` | `flex-wrap` | 是否换行 | ✅ |
| `gap` | `gap` | 行/列间距 | ✅ |
| `order` | `order` | 子元素排序 | ⏳ 缓 |
| `flexGrow` | `flex-grow` | 生长比例 | ✅ |
| `flexShrink` | `flex-shrink` | 收缩比例 | ✅ |
| `flexBasis` | `flex-basis` | 基准尺寸 | ✅ |

`flex` / `flex-flow` 简写 **不单独设字段**，同上：结构体聚合即简写。

### 3.1 全局容器开关

| 字段 | 对应 CSS | 说明 | 取舍 |
| --- | --- | --- | --- |
| `display` | `display` | `flex` / `block` / `none` | ✅ 只需要「是不是 flex 容器」+ `none` 隐藏 |
| `overflow` | `overflow` | `visible` / `hidden` / `scroll` / `auto` | ✅ |
| `overflowX` | `overflow-x` | 水平溢出处理 | ✅ |
| `overflowY` | `overflow-y` | 垂直溢出处理 | ✅ |

> ⚠️ `display: none` 与 `visibility: hidden` 的区别必须保留：
> - `none` → **不占空间**，不进布局；
> - `hidden` → **仍占空间**，只是不画。
> 这是布局语义差异，不是一个绘制开关，实现时别合并。

---

## 4. 留在 Widget、不进样式表

| 属性 | 原因 |
| --- | --- |
| `width` / `height` | 几何层，布局阶段决定，不是绘制属性 |
| `min-width` / `min-height` | 同上。**注意别在样式表里重复定义一遍** |
| `max-width` / `max-height` | 同上 |
| `margin` 系列 | 外边距是容器布局问题，归父容器的 `gap` / 对齐 |
| `position` / `top` / `right` / `bottom` / `left` / `z-index` | 定位归布局引擎与层级系统，不进控件样式 |
| `clip` | 用 `overflow: hidden` 表达，不单设字段 |
| `box-sizing` | 直接固定 `border-box`，不给选项 |

---

## 5. 砍掉的属性（完整对照）

### 5.1 浏览器/文档流专属 —— 直接砍

| 原分类 | 属性 | 理由 |
| --- | --- | --- |
| 浮动 | `float`、`clear` | 文档流遗留物，GUI 用 Flex 布局 |
| 多列 | `columns`、`column-count`、`column-fill`、`column-gap`、`column-rule`、`column-rule-color`、`column-rule-style`、`column-rule-width`、`column-span`、`column-width` | 文字分栏是排版软件功能，GUI 不需要 |
| 列表 | `list-style`、`list-style-image`、`list-style-position`、`list-style-type` | GUI 列表自己画行，不用 CSS 列表标记 |
| 用户界面 | `appearance`、`icon`、`nav-up`、`nav-down`、`nav-left`、`nav-right`、`nav-index`、`resize` | 浏览器控件专属 / 已废弃 |
| 文本 | `direction`、`unicode-bidi` | 双向文字，本版只做 LTR |
| 文本效果 | `hanging-punctuation`、`punctuation-trim`、`text-emphasis`、`text-outline`、`text-wrap`、`text-align-last`、`text-justify` | 浏览器排版细节，无对应实现 |
| 字体 | `font-variant`、`font-stretch` | 中文字体无此维度 |
| 背景 | `background-attachment` | 语义不对应 |

### 5.2 缓一缓 —— 本版不进表

| 分类 | 属性 | 备注 |
| --- | --- | --- |
| Grid | `grid`、`grid-area`、`grid-auto-*`、`grid-column*`、`grid-row*`、`grid-template*`、`row-gap`、`column-gap`、`gap`(grid 语义) | 二维布局，先吃透 Flexible |
| 2D 转换 | `transform`、`transform-origin`、`translate*`、`scale*`、`rotate`、`skew*`、`matrix` | 动效阶段再做 |
| 3D 转换 | `transform-style`、`perspective`、`perspective-origin`、`backface-visibility`、`translate3d`、`scale3d`、`rotate3d`、`matrix3d` | 同上 |
| 过渡 | `transition`、`transition-property`、`transition-duration`、`transition-timing-function`、`transition-delay` | 同上 |
| 动画 | `@keyframes`、`animation`、`animation-*` | 同上 |
| 字体描述符 | `@font-face` 的 `src`、`unicode-range` | 字体加载走框架自己的资源系统 |

### 5.3 合并 / 复用 —— 不新建类型

| 原属性 | 处理 |
| --- | --- |
| `box-shadow` + `text-shadow` | 合并成**全库唯一**的 `Shadow` 类型，盒子阴影和文字阴影共用 |
| `border-radius`（出现两次：边框节 + 圆角节） | 只保留一处，走 `RadiusMode` |
| `border-top-left-radius` 等四个角 | 归入 `borderRadius` 的四角字段，不单独造类型 |
| `line-height`（出现在文本属性 + 尺寸属性） | 只归 `TextStyle`，尺寸表里删掉 |

---

## 6. 两条硬性约束

1. **同一语义的枚举/结构体，全库只定义一次。** 出现 `ButtonRadiusMode` 和 `RadiusMode` 并存，就是没做到。
2. **数值必须能沿一条单向路径追溯到主题 token。** 只要还有 `switch (RadiusMode::Large) return 16.0f;` 就是没做到 —— 数值只能从 `Theme` 取。
