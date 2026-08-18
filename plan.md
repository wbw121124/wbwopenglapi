# plan.md — 直线粗细不均（11 default 矢量描边）修复计划

## 任务
11 里 `ctx.lineAlgorithm("default")` 的矢量描边线（(60,80)-(660,230) lw4）视觉粗细不均（两头粗中间细）。
12 的 ssaa 也存在 FBO 内容偏窄疑点（x=400 仅 4 行）。每完成一小步：更新本文件 + git commit + git push。

## 步骤
- [x] 11 增加 saveBmp 输出，量化线宽
- [x] 修复 saveBmp 的 y 方向颠倒（11 + 12），重新生成 BMP 验证方向
- [x] 正确方向下量化 default 线宽分布，定位粗细分段
- [x] 修复矢量描边（buildStrokeStrip/drawSolid/投影）根因：三角形分解改为 {al,ar,bl, ar,bl,br}
- [x] 回归 11/12 全绿（09/10 重编译重跑确认全绿）
- [x] 拆除 12 调试块（present/drawSolid DEBUG + 12 cpp m==0 块）
- [x] build/ DLL 复制到 test/
- [x] 最终全量回归 + commit + push

## 诊断记录
- 11_lines.bmp x 列红行分布异常（x=120: 114,164,334,503-506…）→ 发现 BMP 上下颠倒
- saveBmp: glReadPixels 行 0 是 GL 顶行，BMP 行 0 也是顶 → canvas y 与 BMP 行映射为 fh-1-y，图像颠倒
- 12_antialias_off.bmp 实测 (310,400)=(60,76,231) 红、(310,200)=背景，与 canvas 坐标 (310,183) 线位置差 217px → 同样颠倒
- **根因定位（buildStrokeStrip triangle 分解错误）**：out 顺序 {al,ar,bl, al,bl,br}，两三角形共享边 al-bl（左上→右下对角线），右下边 ar-br 附近带无三角形覆盖 → 平行四边形中段只被盖 ~2.3px（应 4px）→ 「两头粗中间细」
  - 手算验证 x=300（t=0.4008）：平行四边形覆盖 NDC y∈[0.5265,0.5396]=3.9px；原分解并集仅 [0.5265,0.5342]=2.3px，缺 [0.5342,0.5396]
  - 修复：{al,ar,bl, ar,bl,br}（共享对角线 ar-bl），并集=完整平行四边形
- 11_lines.bmp 量化（方向修复后）：A 线 x=60-88 段 4 行 → x≈342-377 段仅 2 行 → x=526+ 又 4 行；lw4 理论恒 4.12px
- **修复验证**：buildStrokeStrip 改为 {al,ar,bl, ar,bl,br} 后——
  - 11 A 线 x=60..659 全部恒定 4 行（0 个 2/3 行段）
  - 12 off 斜线 x=90..350 全部恒定 3 行
  - ssaa FBO 竖切 x=400: y906-912 七行完整（修复前仅 906-909 四行）；proj 复核恒为 0.0025/-0.0033（此前疑云排除）
  - 11/12 exit=0 全绿
- **收尾**：调试块全部拆除（hpp present/drawSolid DEBUG、12 cpp m==0 块）；全量回归 09/10/11/12 均 exit=0，stderr 零调试输出；5 张 BMP + 11_lines.bmp 正常；build/ 5 个 DLL 复制到 test/