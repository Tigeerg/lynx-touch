# measure-contour.ps1 - замер НАРИСОВАННОГО контура реза на кадре вьювера.
#
# Мера линии, а не данных: по кадру PNG ищется тёмная линия контура
# (contourColor #101418 на металле остатка) и её центр и эквивалентная ширина
# берутся по каждому столбцу (Axis y) или строке (Axis x) окна съёмки. Два
# прохода: грубый - самый тёмный пиксель окна, затем полоса вокруг линейного
# тренда, в которой центр - взвешенный центроид чернил (фон - максимум полосы),
# а ширина - чернила / пик. Волнистость центра - разброс его остатка от тренда
# (прямой прогон - от прямой, кривая - от скользящего среднего), в пикселях.
#   powershell -File scripts/measure-contour.ps1 -Png img/x.png -X 700 -Y 60 -W 250 -H 170 -Axis y
param(
    [Parameter(Mandatory=$true)][string]$Png,
    [int]$X = 0,
    [int]$Y = 0,
    [int]$W = 1024,
    [int]$H = 600,
    [ValidateSet("x","y")][string]$Axis = "x",
    [ValidateSet("line","edge")][string]$Mode = "line",
    [double]$CenterMin = -1,
    [double]$CenterMax = -1,
    [double]$BandPx = 6,
    [double]$Smooth = 11,
    [string]$Label = ""
)
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing

$bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Png))
$wbmp = $bmp.Width; $hbmp = $bmp.Height
if ($W -le 0 -or $X + $W -gt $wbmp) { $W = $wbmp - $X }
if ($H -le 0 -or $Y + $H -gt $hbmp) { $H = $hbmp - $Y }
# Люминанс sRGB: центру и ширине нужна форма профиля, гамма не трогается.
$lum = New-Object 'double[,]' $W, $H
for ($yy = 0; $yy -lt $H; $yy++) {
    for ($xx = 0; $xx -lt $W; $xx++) {
        $px = $bmp.GetPixel($X + $xx, $Y + $yy)
        $lum[$xx, $yy] = 0.2126 * $px.R + 0.7152 * $px.G + 0.0722 * $px.B
    }
}
$bmp.Dispose()

$scanCount = if ($Axis -eq "x") { $H } else { $W }
$scanLen   = if ($Axis -eq "x") { $W } else { $H }

function GetProfile($s) {
    $p = New-Object 'double[]' $scanLen
    for ($t = 0; $t -lt $scanLen; $t++) {
        $p[$t] = if ($Axis -eq "x") { $lum[$t, $s] } else { $lum[$s, $t] }
    }
    return ,$p
}

# Проход 1: грубый центр - самый тёмный пиксель окна, тренд - прямая по
# медианным точкам (робастно к выпадениям, когда в окне оказывается другая
# тёмная кромка).
$t0 = 0; $t1 = $scanLen - 1
if ($CenterMin -ge 0) { $t0 = [Math]::Max(0, [int][Math]::Round($CenterMin)) }
if ($CenterMax -ge 0) { $t1 = [Math]::Min($scanLen - 1, [int][Math]::Round($CenterMax)) }
$coarse = New-Object 'double[]' $scanCount
for ($s = 0; $s -lt $scanCount; $s++) {
    $p = GetProfile $s
    if ($Mode -eq "edge") {
        $imax = $t0; $vmax = -1.0
        for ($t = [Math]::Max($t0, 1); $t -le [Math]::Min($t1, $scanLen - 2); $t++) {
            $g = [Math]::Abs($p[$t + 1] - $p[$t - 1])
            if ($g -gt $vmax) { $vmax = $g; $imax = $t }
        }
        $coarse[$s] = $imax
    } else {
        $imin = $t0; $vmin = $p[$t0]
        for ($t = $t0; $t -le $t1; $t++) { if ($p[$t] -lt $vmin) { $vmin = $p[$t]; $imin = $t } }
        $coarse[$s] = $imin
    }
}
function FitLine($xsA, $ysA) {
    $n = $xsA.Count
    if ($n -lt 3) { return @(0.0, 0.0) }
    $sx = 0.0; $sy = 0.0; $sxx = 0.0; $sxy = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $sx += $xsA[$i]; $sy += $ysA[$i]; $sxx += $xsA[$i]*$xsA[$i]; $sxy += $xsA[$i]*$ysA[$i]
    }
    $den = $n * $sxx - $sx * $sx
    if ([Math]::Abs($den) -lt 1e-9) { return @(0.0, $sy / $n) }
    $sl = ($n * $sxy - $sx * $sy) / $den
    $ic = ($sy - $sl * $sx) / $n
    return @($sl, $ic)
}
$fit = FitLine (0..($scanCount-1)) $coarse
# Один проход отбраковки: точки дальше 8 px от прямой не тянут её.
$xs2 = @(); $ys2 = @()
for ($s = 0; $s -lt $scanCount; $s++) {
    $pred = $fit[0] * $s + $fit[1]
    if ([Math]::Abs($coarse[$s] - $pred) -le 8.0) { $xs2 += $s; $ys2 += $coarse[$s] }
}
if ($xs2.Count -ge 3) { $fit = FitLine $xs2 $ys2 }

# Проход 2: полоса вокруг тренда; центр - центроид чернил, ширина - чернила/пик.
$centres = New-Object 'double[]' $scanCount
$widths  = New-Object 'double[]' $scanCount
$peaks   = New-Object 'double[]' $scanCount
$valid   = New-Object 'bool[]' $scanCount
for ($s = 0; $s -lt $scanCount; $s++) {
    $pred = $fit[0] * $s + $fit[1]
    $b0 = [Math]::Max($t0, [int][Math]::Floor($pred - $BandPx))
    $b1 = [Math]::Min($t1, [int][Math]::Ceiling($pred + $BandPx))
    if ($b1 -le $b0) { continue }
    $p = GetProfile $s
    if ($Mode -eq "edge") {
        # Подпиксельный край: парабола по |градиенту| вокруг его пика.
        $imax = $b0; $vmax = -1.0
        for ($t = [Math]::Max($b0, 1); $t -le [Math]::Min($b1, $scanLen - 2); $t++) {
            $g = [Math]::Abs($p[$t + 1] - $p[$t - 1])
            if ($g -gt $vmax) { $vmax = $g; $imax = $t }
        }
        $c = [double]$imax
        if ($imax -gt $b0 -and $imax -lt $b1 -and $imax -gt 0) {
            $gm = [Math]::Abs($p[$imax] - $p[$imax - 2])
            $g0 = [Math]::Abs($p[$imax - 1] - $p[$imax + 1])
            $gp = [Math]::Abs($p[$imax + 2] - $p[$imax])
            $den = ($gm - 2.0 * $g0 + $gp)
            if ([Math]::Abs($den) -gt 1e-6) {
                $off = 0.5 * ($gm - $gp) / $den
                if ([Math]::Abs($off) -le 1.0) { $c = $imax + $off }
            }
        }
        $centres[$s] = $c
        $widths[$s] = $vmax
        $peaks[$s] = $vmax
        $valid[$s] = $true
        continue
    }
    $bw = $p[$b0]
    for ($t = $b0; $t -le $b1; $t++) { if ($p[$t] -gt $bw) { $bw = $p[$t] } }
    # Вес - чернила сверх порога: ниже половины перепада фон не тянет центр.
    $thr = $bw - 0.15 * ($bw - ($p | Measure-Object -Minimum).Minimum)
    $num = 0.0; $den = 0.0
    for ($t = $b0; $t -le $b1; $t++) {
        $wgt = [Math]::Max(0.0, $bw - $p[$t])
        if ($p[$t] -lt $thr) { $num += $t * $wgt; $den += $wgt }
    }
    if ($den -lt 1e-6) { continue }
    $centres[$s] = $num / $den
    $ink = 0.0
    for ($t = $b0; $t -le $b1; $t++) { $ink += [Math]::Max(0.0, $bw - $p[$t]) }
    $pk = [Math]::Max($bw - ($p | Measure-Object -Minimum).Minimum, 1e-6)
    $widths[$s] = $ink / $pk
    $peaks[$s] = $pk
    $valid[$s] = $true
}

$ic = @(); $yc = @()
for ($s = 0; $s -lt $scanCount; $s++) { if ($valid[$s]) { $ic += $s; $yc += $centres[$s] } }
$m = $ic.Count
if ($m -lt 5) { Write-Output "мало сканов: $m"; exit 2 }
$fit2 = FitLine $ic $yc
$residLin = New-Object 'double[]' $m
for ($i = 0; $i -lt $m; $i++) { $residLin[$i] = $yc[$i] - ($fit2[0] * $ic[$i] + $fit2[1]) }
$residSmooth = New-Object 'double[]' $m
$half = [int][Math]::Floor($Smooth / 2)
for ($i = 0; $i -lt $m; $i++) {
    $lo = [Math]::Max(0, $i - $half); $hi = [Math]::Min($m - 1, $i + $half)
    $sum = 0.0; $cnt = 0
    for ($j = $lo; $j -le $hi; $j++) { $sum += $yc[$j]; $cnt++ }
    $residSmooth[$i] = $yc[$i] - $sum / $cnt
}
function Stat($arr) {
    $avg = ($arr | Measure-Object -Average).Average
    $mn = ($arr | Measure-Object -Minimum).Minimum
    $mx = ($arr | Measure-Object -Maximum).Maximum
    $ss = 0.0; foreach ($v in $arr) { $ss += ($v - $avg) * ($v - $avg) }
    $sd = [Math]::Sqrt($ss / [Math]::Max(1, $arr.Count))
    return [pscustomobject]@{ Mean = $avg; Sd = $sd; Span = $mx - $mn }
}
$stLin = Stat $residLin
$stSm  = Stat $residSmooth
$ws = @(); $pks = @()
for ($s = 0; $s -lt $scanCount; $s++) { if ($valid[$s]) { $ws += $widths[$s]; $pks += $peaks[$s] } }
$stW = Stat $ws
$stP = Stat $pks
# Модуляция подпиксельной фазой: тонкая линия на сетке пульсирует - её ширина и
# пик зависят от доли пикселя под центром. Это чисто рисунок, не геометрия:
# размах средних ширины и пика по четырём фазовым корзинам.
$nb = 4
$wBin = New-Object 'double[]' $nb; $wCnt = New-Object 'int[]' $nb
$pBin = New-Object 'double[]' $nb
for ($s = 0; $s -lt $scanCount; $s++) {
    if (-not $valid[$s]) { continue }
    $fr = $centres[$s] - [Math]::Floor($centres[$s])
    $bi = [Math]::Min($nb - 1, [int][Math]::Floor($fr * $nb))
    $wBin[$bi] += $widths[$s]; $pBin[$bi] += $peaks[$s]; $wCnt[$bi]++
}
$wMod = 0.0; $pMod = 0.0
$wMeans = @(); $pMeans = @()
for ($b = 0; $b -lt $nb; $b++) {
    if ($wCnt[$b] -gt 0) { $wMeans += $wBin[$b] / $wCnt[$b]; $pMeans += $pBin[$b] / $wCnt[$b] }
}
if ($wMeans.Count -ge 2) {
    $wm = ($wMeans | Measure-Object -Average).Average
    $wMod = (($wMeans | Measure-Object -Maximum).Maximum - ($wMeans | Measure-Object -Minimum).Minimum) / [Math]::Max($wm, 1e-6)
    $pm = ($pMeans | Measure-Object -Average).Average
    $pMod = (($pMeans | Measure-Object -Maximum).Maximum - ($pMeans | Measure-Object -Minimum).Minimum) / [Math]::Max($pm, 1e-6)
}
$out = [ordered]@{
    label = $Label
    png = (Split-Path $Png -Leaf)
    crop = "$X,$Y $W x $H"
    axis = $Axis
    scans = $m
    slope_px_per_scan = [Math]::Round($fit2[0], 5)
    centre_sd_dev_lin = [Math]::Round($stLin.Sd, 4)
    centre_span_dev_lin = [Math]::Round($stLin.Span, 4)
    centre_sd_dev_smooth = [Math]::Round($stSm.Sd, 4)
    centre_span_dev_smooth = [Math]::Round($stSm.Span, 4)
    width_mean = [Math]::Round($stW.Mean, 4)
    width_sd = [Math]::Round($stW.Sd, 4)
    width_span = [Math]::Round($stW.Span, 4)
    peak_mean = [Math]::Round($stP.Mean, 2)
    peak_sd = [Math]::Round($stP.Sd, 4)
    width_phase_mod = [Math]::Round($wMod, 4)
    peak_phase_mod = [Math]::Round($pMod, 4)
}
$out.GetEnumerator() | ForEach-Object { "{0}={1}" -f $_.Key, $_.Value }
