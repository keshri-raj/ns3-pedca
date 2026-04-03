$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$resultsRoot = "C:\ns-3\results"
$outputDir = Join-Path $resultsRoot "analysis"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

function Get-MetricMap {
    param([string]$CsvPath)

    $map = @{}
    Import-Csv -Path $CsvPath | ForEach-Object {
        $map[$_.metric] = [double]$_.value
    }
    return $map
}

function Escape-Xml {
    param([string]$Text)

    return [System.Security.SecurityElement]::Escape($Text)
}

function Write-BarChartSvg {
    param(
        [string]$Path,
        [string]$Title,
        [array]$Rows,
        [array]$Series,
        [string]$ValueFormat = "N2"
    )

    $width = 1400
    $rowHeight = 36
    $top = 90
    $left = 350
    $right = 120
    $bottom = 70
    $plotWidth = $width - $left - $right
    $height = $top + ($Rows.Count * $rowHeight) + $bottom
    $zeroX = $left + ($plotWidth / 2)

    $allValues = @()
    foreach ($row in $Rows) {
        foreach ($series in $Series) {
            $allValues += [math]::Abs([double]$row[$series.Key])
        }
    }
    $maxAbs = [math]::Max(1.0, ($allValues | Measure-Object -Maximum).Maximum)

    $svg = New-Object System.Collections.Generic.List[string]
    $svg.Add('<?xml version="1.0" encoding="UTF-8"?>')
    $svg.Add("<svg xmlns='http://www.w3.org/2000/svg' width='$width' height='$height' viewBox='0 0 $width $height'>")
    $svg.Add("<rect width='100%' height='100%' fill='white'/>")
    $svg.Add("<text x='24' y='38' font-family='Segoe UI, Arial, sans-serif' font-size='26' font-weight='700'>$(Escape-Xml $Title)</text>")
    $svg.Add("<text x='24' y='62' font-family='Segoe UI, Arial, sans-serif' font-size='14' fill='#555'>Negative bars favor both-links (Dist0). Positive bars favor one-link (Dist1).</text>")

    foreach ($tick in @(-1.0, -0.5, 0.0, 0.5, 1.0)) {
        $value = $tick * $maxAbs
        $x = $zeroX + ($tick * ($plotWidth / 2))
        $svg.Add("<line x1='$x' y1='$top' x2='$x' y2='" + ($height - $bottom) + "' stroke='#dddddd' stroke-width='1'/>")
        $svg.Add("<text x='$x' y='" + ($height - 28) + "' text-anchor='middle' font-family='Segoe UI, Arial, sans-serif' font-size='12' fill='#666'>$([string]::Format('{0:' + $ValueFormat + '}', $value))</text>")
    }
    $svg.Add("<line x1='$zeroX' y1='$top' x2='$zeroX' y2='" + ($height - $bottom) + "' stroke='#666666' stroke-width='1.5'/>")

    $legendX = $left
    foreach ($series in $Series) {
        $svg.Add("<rect x='$legendX' y='70' width='16' height='16' fill='$($series.Color)'/>")
        $svg.Add("<text x='" + ($legendX + 24) + "' y='83' font-family='Segoe UI, Arial, sans-serif' font-size='13'>$(Escape-Xml $series.Name)</text>")
        $legendX += 230
    }

    for ($i = 0; $i -lt $Rows.Count; $i++) {
        $row = $Rows[$i]
        $y = $top + ($i * $rowHeight)
        $svg.Add("<text x='" + ($left - 10) + "' y='" + ($y + 18) + "' text-anchor='end' font-family='Segoe UI, Arial, sans-serif' font-size='12'>$(Escape-Xml $row.Label)</text>")

        $seriesCount = [math]::Max(1, $Series.Count)
        $bandHeight = 24
        $barHeight = [math]::Max(6, [math]::Floor($bandHeight / $seriesCount) - 2)
        for ($j = 0; $j -lt $Series.Count; $j++) {
            $series = $Series[$j]
            $value = [double]$row[$series.Key]
            $barY = $y + 4 + ($j * ($barHeight + 2))
            $barWidth = ([math]::Abs($value) / $maxAbs) * ($plotWidth / 2)
            if ($value -ge 0) {
                $x = $zeroX
            } else {
                $x = $zeroX - $barWidth
            }
            $svg.Add("<rect x='$x' y='$barY' width='$barWidth' height='$barHeight' fill='$($series.Color)' rx='2' ry='2'/>")
            $textAnchor = if ($value -ge 0) { "start" } else { "end" }
            $textX = if ($value -ge 0) { $x + $barWidth + 6 } else { $x - 6 }
            $svg.Add("<text x='$textX' y='" + ($barY + $barHeight - 2) + "' text-anchor='$textAnchor' font-family='Segoe UI, Arial, sans-serif' font-size='11' fill='#333'>$([string]::Format('{0:' + $ValueFormat + '}', $value))</text>")
        }
    }

    $svg.Add("</svg>")
    Set-Content -Path $Path -Value $svg
}

function Get-HeatColor {
    param(
        [double]$Value,
        [double]$MaxAbs
    )

    if ($MaxAbs -le 0) {
        return "#f3f4f6"
    }

    $norm = [math]::Min(1.0, [math]::Abs($Value) / $MaxAbs)
    if ($Value -lt 0) {
        $light = 95 - [math]::Round($norm * 38)
        return "hsl(210, 70%, $light%)"
    }
    if ($Value -gt 0) {
        $light = 95 - [math]::Round($norm * 38)
        return "hsl(12, 78%, $light%)"
    }
    return "#f3f4f6"
}

function Write-HeatmapSvg {
    param(
        [string]$Path,
        [string]$Title,
        [array]$Rows,
        [array]$Columns,
        [string]$ValueFormat = "N2",
        [string]$Subtitle = "Blue tiles favor one-link (lower value). Orange tiles favor both-links or indicate increases."
    )

    $labelWidth = 360
    $cellWidth = 150
    $cellHeight = 34
    $top = 110
    $left = 30
    $bottom = 50
    $width = $left + $labelWidth + ($Columns.Count * $cellWidth) + 30
    $height = $top + ($Rows.Count * $cellHeight) + $bottom

    $allValues = @()
    foreach ($row in $Rows) {
        foreach ($column in $Columns) {
            $allValues += [math]::Abs([double]$row[$column.Key])
        }
    }
    $maxAbs = [math]::Max(1.0, ($allValues | Measure-Object -Maximum).Maximum)

    $svg = New-Object System.Collections.Generic.List[string]
    $svg.Add('<?xml version="1.0" encoding="UTF-8"?>')
    $svg.Add("<svg xmlns='http://www.w3.org/2000/svg' width='$width' height='$height' viewBox='0 0 $width $height'>")
    $svg.Add("<rect width='100%' height='100%' fill='white'/>")
    $svg.Add("<text x='24' y='38' font-family='Segoe UI, Arial, sans-serif' font-size='26' font-weight='700'>$(Escape-Xml $Title)</text>")
    $svg.Add("<text x='24' y='62' font-family='Segoe UI, Arial, sans-serif' font-size='14' fill='#555'>$(Escape-Xml $Subtitle)</text>")

    for ($c = 0; $c -lt $Columns.Count; $c++) {
        $column = $Columns[$c]
        $x = $left + $labelWidth + ($c * $cellWidth)
        $svg.Add("<text x='" + ($x + ($cellWidth / 2)) + "' y='96' text-anchor='middle' font-family='Segoe UI, Arial, sans-serif' font-size='12' font-weight='600'>$(Escape-Xml $column.Name)</text>")
    }

    for ($r = 0; $r -lt $Rows.Count; $r++) {
        $row = $Rows[$r]
        $y = $top + ($r * $cellHeight)
        $svg.Add("<text x='" + ($left + $labelWidth - 10) + "' y='" + ($y + 22) + "' text-anchor='end' font-family='Segoe UI, Arial, sans-serif' font-size='12'>$(Escape-Xml $row.Label)</text>")

        for ($c = 0; $c -lt $Columns.Count; $c++) {
            $column = $Columns[$c]
            $x = $left + $labelWidth + ($c * $cellWidth)
            $value = [double]$row[$column.Key]
            $fill = Get-HeatColor -Value $value -MaxAbs $maxAbs
            $svg.Add("<rect x='$x' y='$y' width='" + ($cellWidth - 6) + "' height='" + ($cellHeight - 4) + "' fill='$fill' stroke='#ffffff' stroke-width='1' rx='4' ry='4'/>")
            $svg.Add("<text x='" + ($x + (($cellWidth - 6) / 2)) + "' y='" + ($y + 21) + "' text-anchor='middle' font-family='Segoe UI, Arial, sans-serif' font-size='11' fill='#111'>$([string]::Format('{0:' + $ValueFormat + '}', $value))</text>")
        }
    }

    $legendY = $height - 28
    $legendX = $left + $labelWidth
    $svg.Add("<rect x='$legendX' y='$legendY' width='18' height='18' fill='#93c5fd'/>")
    $svg.Add("<text x='" + ($legendX + 26) + "' y='" + ($legendY + 13) + "' font-family='Segoe UI, Arial, sans-serif' font-size='12'>Favors one-link / lower tail</text>")
    $svg.Add("<rect x='" + ($legendX + 220) + "' y='$legendY' width='18' height='18' fill='#fdba74'/>")
    $svg.Add("<text x='" + ($legendX + 246) + "' y='" + ($legendY + 13) + "' font-family='Segoe UI, Arial, sans-serif' font-size='12'>Favors both-links / higher tail</text>")
    $svg.Add("</svg>")

    Set-Content -Path $Path -Value $svg
}

function Write-HeatmapPng {
    param(
        [string]$Path,
        [string]$Title,
        [array]$Rows,
        [array]$Columns,
        [string]$ValueFormat = "N2",
        [string]$Subtitle = "Blue means one-link reduced the metric. Orange means it increased."
    )

    $labelWidth = 360
    $cellWidth = 150
    $cellHeight = 34
    $top = 110
    $left = 30
    $bottom = 50
    $width = $left + $labelWidth + ($Columns.Count * $cellWidth) + 30
    $height = $top + ($Rows.Count * $cellHeight) + $bottom

    $allValues = @()
    foreach ($row in $Rows) {
        foreach ($column in $Columns) {
            $allValues += [math]::Abs([double]$row[$column.Key])
        }
    }
    $maxAbs = [math]::Max(1.0, ($allValues | Measure-Object -Maximum).Maximum)

    $bmp = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $graphics.Clear([System.Drawing.Color]::White)

    $titleFont = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
    $subtitleFont = New-Object System.Drawing.Font("Segoe UI", 10)
    $labelFont = New-Object System.Drawing.Font("Segoe UI", 9)
    $headerFont = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $valueFont = New-Object System.Drawing.Font("Segoe UI", 8)

    $blackBrush = [System.Drawing.Brushes]::Black
    $grayBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(85,85,85))
    $gridPen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 1)

    $graphics.DrawString($Title, $titleFont, $blackBrush, 24, 20)
    $graphics.DrawString($Subtitle, $subtitleFont, $grayBrush, 24, 52)

    for ($c = 0; $c -lt $Columns.Count; $c++) {
        $column = $Columns[$c]
        $x = $left + $labelWidth + ($c * $cellWidth)
        $rect = New-Object System.Drawing.RectangleF($x, 78, ($cellWidth - 6), 24)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $graphics.DrawString($column.Name, $headerFont, $blackBrush, $rect, $sf)
    }

    for ($r = 0; $r -lt $Rows.Count; $r++) {
        $row = $Rows[$r]
        $y = $top + ($r * $cellHeight)
        $labelRect = New-Object System.Drawing.RectangleF($left, ($y + 6), ($labelWidth - 12), 22)
        $lsf = New-Object System.Drawing.StringFormat
        $lsf.Alignment = [System.Drawing.StringAlignment]::Far
        $graphics.DrawString($row.Label, $labelFont, $blackBrush, $labelRect, $lsf)

        for ($c = 0; $c -lt $Columns.Count; $c++) {
            $column = $Columns[$c]
            $x = $left + $labelWidth + ($c * $cellWidth)
            $value = [double]$row[$column.Key]
            $htmlColor = Get-HeatColor -Value $value -MaxAbs $maxAbs
            $cellColor = [System.Drawing.ColorTranslator]::FromHtml($htmlColor)
            $brush = New-Object System.Drawing.SolidBrush($cellColor)
            $rect = New-Object System.Drawing.Rectangle($x, $y, ($cellWidth - 6), ($cellHeight - 4))
            $graphics.FillRectangle($brush, $rect)
            $graphics.DrawRectangle($gridPen, $rect)

            $textRect = New-Object System.Drawing.RectangleF($x, ($y + 7), ($cellWidth - 6), 18)
            $tsf = New-Object System.Drawing.StringFormat
            $tsf.Alignment = [System.Drawing.StringAlignment]::Center
            $graphics.DrawString(([string]::Format('{0:' + $ValueFormat + '}', $value)), $valueFont, $blackBrush, $textRect, $tsf)
            $brush.Dispose()
        }
    }

    $legendY = $height - 30
    $blueBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#93c5fd"))
    $orangeBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#fdba74"))
    $graphics.FillRectangle($blueBrush, 390, $legendY, 18, 18)
    $graphics.DrawString("Favors one-link / lower value", $subtitleFont, $blackBrush, 416, $legendY + 1)
    $graphics.FillRectangle($orangeBrush, 670, $legendY, 18, 18)
    $graphics.DrawString("Favors both-links / higher value", $subtitleFont, $blackBrush, 696, $legendY + 1)

    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)

    $blueBrush.Dispose()
    $orangeBrush.Dispose()
    $gridPen.Dispose()
    $grayBrush.Dispose()
    $titleFont.Dispose()
    $subtitleFont.Dispose()
    $labelFont.Dispose()
    $headerFont.Dispose()
    $valueFont.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
}

function Write-DelayComparisonPng {
    param(
        [string]$Path,
        [string]$Title,
        [array]$Rows
    )

    $labelWidth = 320
    $barAreaWidth = 820
    $groupHeight = 34
    $rowGap = 16
    $top = 110
    $left = 30
    $bottom = 70
    $width = $left + $labelWidth + $barAreaWidth + 40
    $height = $top + ($Rows.Count * ($groupHeight + $rowGap)) + $bottom

    $maxValue = 1.0
    foreach ($row in $Rows) {
        $maxValue = [math]::Max($maxValue, [double]$row.dist0_vo_delay_p99)
        $maxValue = [math]::Max($maxValue, [double]$row.dist1_vo_delay_p99)
    }

    $bmp = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $graphics.Clear([System.Drawing.Color]::White)

    $titleFont = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
    $subtitleFont = New-Object System.Drawing.Font("Segoe UI", 10)
    $labelFont = New-Object System.Drawing.Font("Segoe UI", 9)
    $legendFont = New-Object System.Drawing.Font("Segoe UI", 8)
    $valueFont = New-Object System.Drawing.Font("Segoe UI", 8)

    $blackBrush = [System.Drawing.Brushes]::Black
    $grayBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(85,85,85))
    $axisPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(210,210,210), 1)

    $cDist0P95 = [System.Drawing.ColorTranslator]::FromHtml("#2563eb")
    $cDist1P95 = [System.Drawing.ColorTranslator]::FromHtml("#16a34a")
    $cDist0P99 = [System.Drawing.ColorTranslator]::FromHtml("#f59e0b")
    $cDist1P99 = [System.Drawing.ColorTranslator]::FromHtml("#dc2626")
    $bDist0P95 = New-Object System.Drawing.SolidBrush($cDist0P95)
    $bDist1P95 = New-Object System.Drawing.SolidBrush($cDist1P95)
    $bDist0P99 = New-Object System.Drawing.SolidBrush($cDist0P99)
    $bDist1P99 = New-Object System.Drawing.SolidBrush($cDist1P99)

    $graphics.DrawString($Title, $titleFont, $blackBrush, 24, 20)
    $graphics.DrawString("Absolute VO delay tails by case. Longer bars indicate worse delay.", $subtitleFont, $grayBrush, 24, 52)

    for ($tick = 0; $tick -le 4; $tick++) {
        $value = ($tick / 4.0) * $maxValue
        $x = $left + $labelWidth + (($tick / 4.0) * $barAreaWidth)
        $graphics.DrawLine($axisPen, $x, $top, $x, $height - $bottom + 10)
        $tickRect = New-Object System.Drawing.RectangleF(($x - 25), ($height - 34), 50, 18)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $graphics.DrawString(([string]::Format('{0:N0}', $value)), $legendFont, $grayBrush, $tickRect, $sf)
    }

    $legendItems = @(
        @{ Brush = $bDist0P95; Label = "Dist0 VO p95" },
        @{ Brush = $bDist1P95; Label = "Dist1 VO p95" },
        @{ Brush = $bDist0P99; Label = "Dist0 VO p99" },
        @{ Brush = $bDist1P99; Label = "Dist1 VO p99" }
    )
    $legendX = $left + $labelWidth
    foreach ($item in $legendItems) {
        $graphics.FillRectangle($item.Brush, $legendX, 78, 14, 14)
        $graphics.DrawString($item.Label, $legendFont, $blackBrush, $legendX + 20, 77)
        $legendX += 145
    }

    for ($i = 0; $i -lt $Rows.Count; $i++) {
        $row = $Rows[$i]
        $y = $top + ($i * ($groupHeight + $rowGap))
        $labelRect = New-Object System.Drawing.RectangleF($left, ($y + 6), ($labelWidth - 12), 18)
        $lsf = New-Object System.Drawing.StringFormat
        $lsf.Alignment = [System.Drawing.StringAlignment]::Far
        $graphics.DrawString($row.Label, $labelFont, $blackBrush, $labelRect, $lsf)

        $barBaseX = $left + $labelWidth
        $barHeight = 6
        $values = @(
            @{ Value = [double]$row.dist0_vo_delay_p95; Brush = $bDist0P95; Offset = 0 },
            @{ Value = [double]$row.dist1_vo_delay_p95; Brush = $bDist1P95; Offset = 8 },
            @{ Value = [double]$row.dist0_vo_delay_p99; Brush = $bDist0P99; Offset = 18 },
            @{ Value = [double]$row.dist1_vo_delay_p99; Brush = $bDist1P99; Offset = 26 }
        )

        foreach ($entry in $values) {
            $barWidth = ($entry.Value / $maxValue) * $barAreaWidth
            $graphics.FillRectangle($entry.Brush, $barBaseX, ($y + $entry.Offset), $barWidth, $barHeight)
            $graphics.DrawString(([string]::Format('{0:N0}', $entry.Value)), $valueFont, $blackBrush, ($barBaseX + $barWidth + 6), ($y + $entry.Offset - 3))
        }
    }

    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)

    $bDist0P95.Dispose()
    $bDist1P95.Dispose()
    $bDist0P99.Dispose()
    $bDist1P99.Dispose()
    $axisPen.Dispose()
    $grayBrush.Dispose()
    $titleFont.Dispose()
    $subtitleFont.Dispose()
    $labelFont.Dispose()
    $legendFont.Dispose()
    $valueFont.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
}

function Write-DelayGroupedBarPng {
    param(
        [string]$Path,
        [string]$Title,
        [array]$Rows,
        [string]$Dist0Key,
        [string]$Dist1Key
    )

    $left = 70
    $top = 70
    $bottom = 90
    $right = 30
    $plotWidth = 1200
    $plotHeight = 520
    $width = $left + $plotWidth + $right
    $height = $top + $plotHeight + $bottom

    $maxValue = 1.0
    foreach ($row in $Rows) {
        $maxValue = [math]::Max($maxValue, [double]$row[$Dist0Key])
        $maxValue = [math]::Max($maxValue, [double]$row[$Dist1Key])
    }
    $maxValue = [math]::Ceiling($maxValue / 50.0) * 50.0

    $bmp = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $graphics.Clear([System.Drawing.Color]::White)

    $titleFont = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
    $axisFont = New-Object System.Drawing.Font("Segoe UI", 9)
    $tickFont = New-Object System.Drawing.Font("Segoe UI", 8)
    $legendFont = New-Object System.Drawing.Font("Segoe UI", 9)

    $axisPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(60,60,60), 1.2)
    $gridPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(225,225,225), 1)
    $dist0Brush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#2563eb"))
    $dist1Brush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#16a34a"))
    $blackBrush = [System.Drawing.Brushes]::Black
    $grayBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(90,90,90))

    $graphics.DrawString($Title, $titleFont, $blackBrush, 24, 20)

    $plotLeft = $left
    $plotTop = $top
    $plotBottom = $top + $plotHeight
    $plotRight = $left + $plotWidth

    for ($tick = 0; $tick -le 5; $tick++) {
        $value = ($tick / 5.0) * $maxValue
        $y = $plotBottom - (($value / $maxValue) * $plotHeight)
        $graphics.DrawLine($gridPen, $plotLeft, $y, $plotRight, $y)
        $graphics.DrawString(([string]::Format('{0:N0}', $value)), $tickFont, $grayBrush, 8, ($y - 7))
    }

    $graphics.DrawLine($axisPen, $plotLeft, $plotTop, $plotLeft, $plotBottom)
    $graphics.DrawLine($axisPen, $plotLeft, $plotBottom, $plotRight, $plotBottom)

    $slotWidth = $plotWidth / [math]::Max(1, $Rows.Count)
    $barWidth = [math]::Min(24, ($slotWidth - 16) / 2)

    for ($i = 0; $i -lt $Rows.Count; $i++) {
        $row = $Rows[$i]
        $centerX = $plotLeft + ($i * $slotWidth) + ($slotWidth / 2)
        $d0 = [double]$row[$Dist0Key]
        $d1 = [double]$row[$Dist1Key]
        $d0Height = ($d0 / $maxValue) * $plotHeight
        $d1Height = ($d1 / $maxValue) * $plotHeight

        $graphics.FillRectangle($dist0Brush, ($centerX - $barWidth - 3), ($plotBottom - $d0Height), $barWidth, $d0Height)
        $graphics.FillRectangle($dist1Brush, ($centerX + 3), ($plotBottom - $d1Height), $barWidth, $d1Height)

        $labelRect = New-Object System.Drawing.RectangleF(($centerX - 30), ($plotBottom + 8), 60, 28)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $graphics.DrawString($row.CaseLabel, $axisFont, $blackBrush, $labelRect, $sf)
    }

    $graphics.DrawString("Client Configuration (P-EDCA:EDCA)", $axisFont, $grayBrush, ($plotLeft + 420), ($height - 28))
    $graphics.DrawString("Delay (ms)", $axisFont, $grayBrush, 8, 6)

    $legendX = $plotRight - 190
    $legendY = 24
    $graphics.FillRectangle($dist0Brush, $legendX, $legendY, 14, 14)
    $graphics.DrawString("Dist0", $legendFont, $blackBrush, ($legendX + 20), ($legendY - 1))
    $graphics.FillRectangle($dist1Brush, ($legendX + 80), $legendY, 14, 14)
    $graphics.DrawString("Dist1", $legendFont, $blackBrush, ($legendX + 100), ($legendY - 1))

    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)

    $axisPen.Dispose()
    $gridPen.Dispose()
    $dist0Brush.Dispose()
    $dist1Brush.Dispose()
    $grayBrush.Dispose()
    $titleFont.Dispose()
    $axisFont.Dispose()
    $tickFont.Dispose()
    $legendFont.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
}

function Write-LineComparisonPng {
    param(
        [string]$Path,
        [string]$Title,
        [array]$Rows,
        [string]$Dist0Key,
        [string]$Dist1Key,
        [string]$YAxisLabel = "Delay (ms)"
    )

    $left = 80
    $top = 80
    $bottom = 110
    $right = 40
    $plotWidth = 1200
    $plotHeight = 520
    $width = $left + $plotWidth + $right
    $height = $top + $plotHeight + $bottom

    $maxValue = [double]::MinValue
    $minValue = [double]::MaxValue
    foreach ($row in $Rows) {
        $v0 = [double]$row[$Dist0Key]
        $v1 = [double]$row[$Dist1Key]
        $maxValue = [math]::Max($maxValue, $v0)
        $maxValue = [math]::Max($maxValue, $v1)
        $minValue = [math]::Min($minValue, $v0)
        $minValue = [math]::Min($minValue, $v1)
    }
    if ($minValue -eq [double]::MaxValue) { $minValue = 0 }
    if ($maxValue -eq [double]::MinValue) { $maxValue = 1 }
    $range = [math]::Max(20.0, $maxValue - $minValue)
    $padding = [math]::Max(10.0, $range * 0.10)
    $axisMin = [math]::Max(0.0, $minValue - $padding)
    $axisMax = $maxValue + $padding
    $axisRange = [math]::Max(1.0, $axisMax - $axisMin)

    $bmp = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $graphics.Clear([System.Drawing.Color]::White)

    $titleFont = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
    $axisFont = New-Object System.Drawing.Font("Segoe UI", 9)
    $tickFont = New-Object System.Drawing.Font("Segoe UI", 8)
    $legendFont = New-Object System.Drawing.Font("Segoe UI", 9)
    $blackBrush = [System.Drawing.Brushes]::Black
    $grayBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(90,90,90))

    $axisPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(60,60,60), 1.2)
    $gridPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(225,225,225), 1)
    $dist0Pen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#2563eb"), 2.5)
    $dist1Pen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#dc2626"), 2.5)
    $dist0Brush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#2563eb"))
    $dist1Brush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#dc2626"))

    $graphics.DrawString($Title, $titleFont, $blackBrush, 24, 20)
    $graphics.DrawString("Blue = Dist0, Red = Dist1", $axisFont, $grayBrush, 24, 52)

    $plotLeft = $left
    $plotTop = $top
    $plotBottom = $top + $plotHeight
    $plotRight = $left + $plotWidth

    for ($tick = 0; $tick -le 5; $tick++) {
        $value = $axisMin + (($tick / 5.0) * $axisRange)
        $y = $plotBottom - ((($value - $axisMin) / $axisRange) * $plotHeight)
        $graphics.DrawLine($gridPen, $plotLeft, $y, $plotRight, $y)
        $graphics.DrawString(([string]::Format('{0:N0}', $value)), $tickFont, $grayBrush, 10, ($y - 7))
    }

    $graphics.DrawLine($axisPen, $plotLeft, $plotTop, $plotLeft, $plotBottom)
    $graphics.DrawLine($axisPen, $plotLeft, $plotBottom, $plotRight, $plotBottom)

    $slotWidth = if ($Rows.Count -gt 1) { $plotWidth / ($Rows.Count - 1) } else { $plotWidth }
    $dist0Points = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
    $dist1Points = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'

    for ($i = 0; $i -lt $Rows.Count; $i++) {
        $row = $Rows[$i]
        $x = $plotLeft + ($i * $slotWidth)
        $y0 = $plotBottom - ((([double]$row[$Dist0Key] - $axisMin) / $axisRange) * $plotHeight)
        $y1 = $plotBottom - ((([double]$row[$Dist1Key] - $axisMin) / $axisRange) * $plotHeight)
        $dist0Points.Add((New-Object System.Drawing.PointF([float]$x, [float]$y0)))
        $dist1Points.Add((New-Object System.Drawing.PointF([float]$x, [float]$y1)))

        $labelRect = New-Object System.Drawing.RectangleF(($x - 34), ($plotBottom + 8), 68, 30)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $graphics.DrawString($row.CaseLabel, $axisFont, $blackBrush, $labelRect, $sf)
    }

    if ($dist0Points.Count -gt 1) {
        $graphics.DrawLines($dist0Pen, $dist0Points.ToArray())
        $graphics.DrawLines($dist1Pen, $dist1Points.ToArray())
    }

    foreach ($point in $dist0Points) {
        $graphics.FillEllipse($dist0Brush, ($point.X - 4), ($point.Y - 4), 8, 8)
    }
    foreach ($point in $dist1Points) {
        $graphics.FillEllipse($dist1Brush, ($point.X - 4), ($point.Y - 4), 8, 8)
    }

    $graphics.DrawString("Client Configuration (P-EDCA:EDCA)", $axisFont, $grayBrush, ($plotLeft + 410), ($height - 28))
    $graphics.DrawString($YAxisLabel, $axisFont, $grayBrush, 8, 8)

    $legendX = $plotRight - 170
    $legendY = 24
    $graphics.DrawLine($dist0Pen, $legendX, ($legendY + 7), ($legendX + 20), ($legendY + 7))
    $graphics.FillEllipse($dist0Brush, ($legendX + 6), ($legendY + 3), 8, 8)
    $graphics.DrawString("Dist0", $legendFont, $blackBrush, ($legendX + 28), ($legendY - 1))
    $graphics.DrawLine($dist1Pen, ($legendX + 80), ($legendY + 7), ($legendX + 100), ($legendY + 7))
    $graphics.FillEllipse($dist1Brush, ($legendX + 86), ($legendY + 3), 8, 8)
    $graphics.DrawString("Dist1", $legendFont, $blackBrush, ($legendX + 108), ($legendY - 1))

    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)

    $axisPen.Dispose()
    $gridPen.Dispose()
    $dist0Pen.Dispose()
    $dist1Pen.Dispose()
    $dist0Brush.Dispose()
    $dist1Brush.Dispose()
    $grayBrush.Dispose()
    $titleFont.Dispose()
    $axisFont.Dispose()
    $tickFont.Dispose()
    $legendFont.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
}

function Get-OrderedRows {
    param([array]$Rows)

    return $Rows | Sort-Object {
        if ($_.CaseLabel -match '^(\d+):(\d+)$') {
            [int]$matches[1] * 1000 + [int]$matches[2]
        } else {
            999999
        }
    }
}

function Write-SimpleGroupedBarPng {
    param(
        [string]$Path,
        [string]$Title,
        [array]$Rows,
        [string]$Dist0Key,
        [string]$Dist1Key,
        [string]$YAxisLabel = "Value"
    )

    $left = 90
    $top = 80
    $bottom = 110
    $right = 40
    $plotWidth = 1200
    $plotHeight = 520
    $width = $left + $plotWidth + $right
    $height = $top + $plotHeight + $bottom

    $maxValue = 1.0
    foreach ($row in $Rows) {
        $maxValue = [math]::Max($maxValue, [double]$row[$Dist0Key])
        $maxValue = [math]::Max($maxValue, [double]$row[$Dist1Key])
    }
    $axisMax = [math]::Ceiling($maxValue / 50.0) * 50.0

    $bmp = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $graphics.Clear([System.Drawing.Color]::White)

    $titleFont = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
    $axisFont = New-Object System.Drawing.Font("Segoe UI", 9)
    $tickFont = New-Object System.Drawing.Font("Segoe UI", 8)
    $legendFont = New-Object System.Drawing.Font("Segoe UI", 9)
    $blackBrush = [System.Drawing.Brushes]::Black
    $grayBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(90,90,90))

    $axisPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(60,60,60), 1.2)
    $gridPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(225,225,225), 1)
    $dist0Brush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#2563eb"))
    $dist1Brush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#dc2626"))

    $graphics.DrawString($Title, $titleFont, $blackBrush, 24, 20)
    $graphics.DrawString("Blue = Dist0, Red = Dist1", $axisFont, $grayBrush, 24, 52)

    $plotLeft = $left
    $plotTop = $top
    $plotBottom = $top + $plotHeight
    $plotRight = $left + $plotWidth

    for ($tick = 0; $tick -le 5; $tick++) {
        $value = ($tick / 5.0) * $axisMax
        $y = $plotBottom - (($value / $axisMax) * $plotHeight)
        $graphics.DrawLine($gridPen, $plotLeft, $y, $plotRight, $y)
        $graphics.DrawString(([string]::Format('{0:N0}', $value)), $tickFont, $grayBrush, 10, ($y - 7))
    }

    $graphics.DrawLine($axisPen, $plotLeft, $plotTop, $plotLeft, $plotBottom)
    $graphics.DrawLine($axisPen, $plotLeft, $plotBottom, $plotRight, $plotBottom)

    $groupWidth = $plotWidth / [math]::Max(1, $Rows.Count)
    $barWidth = [math]::Max(10, [math]::Min(24, ($groupWidth - 20) / 2))

    for ($i = 0; $i -lt $Rows.Count; $i++) {
        $row = $Rows[$i]
        $groupX = $plotLeft + ($i * $groupWidth)
        $centerX = $groupX + ($groupWidth / 2)
        $d0 = [double]$row[$Dist0Key]
        $d1 = [double]$row[$Dist1Key]
        $h0 = ($d0 / $axisMax) * $plotHeight
        $h1 = ($d1 / $axisMax) * $plotHeight

        $graphics.FillRectangle($dist0Brush, ($centerX - $barWidth - 4), ($plotBottom - $h0), $barWidth, $h0)
        $graphics.FillRectangle($dist1Brush, ($centerX + 4), ($plotBottom - $h1), $barWidth, $h1)

        $labelRect = New-Object System.Drawing.RectangleF(($centerX - 34), ($plotBottom + 8), 68, 30)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $graphics.DrawString($row.CaseLabel, $axisFont, $blackBrush, $labelRect, $sf)
    }

    $graphics.DrawString("Client Configuration (P-EDCA:EDCA)", $axisFont, $grayBrush, ($plotLeft + 410), ($height - 28))
    $graphics.DrawString($YAxisLabel, $axisFont, $grayBrush, 8, 8)

    $legendX = $plotRight - 170
    $legendY = 24
    $graphics.FillRectangle($dist0Brush, $legendX, $legendY, 14, 14)
    $graphics.DrawString("Dist0", $legendFont, $blackBrush, ($legendX + 20), ($legendY - 1))
    $graphics.FillRectangle($dist1Brush, ($legendX + 80), $legendY, 14, 14)
    $graphics.DrawString("Dist1", $legendFont, $blackBrush, ($legendX + 100), ($legendY - 1))

    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)

    $axisPen.Dispose()
    $gridPen.Dispose()
    $dist0Brush.Dispose()
    $dist1Brush.Dispose()
    $grayBrush.Dispose()
    $titleFont.Dispose()
    $axisFont.Dispose()
    $tickFont.Dispose()
    $legendFont.Dispose()
    $graphics.Dispose()
    $bmp.Dispose()
}

$pairs = Get-ChildItem -Path $resultsRoot -Recurse -Filter "tail-summary.csv" |
    Group-Object { Split-Path $_.DirectoryName -Parent } |
    Where-Object { $_.Count -eq 2 -and ($_.Group.DirectoryName -contains (Join-Path $_.Name "Dist0")) -and ($_.Group.DirectoryName -contains (Join-Path $_.Name "Dist1")) } |
    Sort-Object Name

$rows = @()
foreach ($pair in $pairs) {
    $base = $pair.Name
    $dist0 = Join-Path $base "Dist0\tail-summary.csv"
    $dist1 = Join-Path $base "Dist1\tail-summary.csv"
    $map0 = Get-MetricMap -CsvPath $dist0
    $map1 = Get-MetricMap -CsvPath $dist1
    $label = $base.Substring($resultsRoot.Length).TrimStart('\')
    $rows += [pscustomobject]@{
        Label = $label
        CaseLabel = if ($label -match 'pedca_(\d+)_edca_(\d+)') { "$($matches[1]):$($matches[2])" } else { $label }
        aggregate_delta = $map1["aggregate_throughput_mbps"] - $map0["aggregate_throughput_mbps"]
        pedca_throughput_delta = $map1["pedca_throughput_mbps"] - $map0["pedca_throughput_mbps"]
        edca_throughput_delta = $map1["edca_throughput_mbps"] - $map0["edca_throughput_mbps"]
        pedca_vo_avg_delta = $map1["avg_pedca_vo_throughput_per_sta_mbps"] - $map0["avg_pedca_vo_throughput_per_sta_mbps"]
        edca_vo_avg_delta = $map1["avg_edca_vo_throughput_per_sta_mbps"] - $map0["avg_edca_vo_throughput_per_sta_mbps"]
        jfi_delta = $map1["jfi"] - $map0["jfi"]
        delay_p95_delta = $map1["delay_p95_ms"] - $map0["delay_p95_ms"]
        delay_p99_delta = $map1["delay_p99_ms"] - $map0["delay_p99_ms"]
        vo_delay_p95_delta = $map1["vo_delay_p95_ms"] - $map0["vo_delay_p95_ms"]
        vo_delay_p99_delta = $map1["vo_delay_p99_ms"] - $map0["vo_delay_p99_ms"]
        pedca_failures_delta = $map1["pedca_failures"] - $map0["pedca_failures"]
        pedca_resets_delta = $map1["pedca_resets"] - $map0["pedca_resets"]
        dist0_aggregate = $map0["aggregate_throughput_mbps"]
        dist1_aggregate = $map1["aggregate_throughput_mbps"]
        dist0_pedca_vo_avg = $map0["avg_pedca_vo_throughput_per_sta_mbps"]
        dist1_pedca_vo_avg = $map1["avg_pedca_vo_throughput_per_sta_mbps"]
        dist0_edca_vo_avg = $map0["avg_edca_vo_throughput_per_sta_mbps"]
        dist1_edca_vo_avg = $map1["avg_edca_vo_throughput_per_sta_mbps"]
        dist0_vo_delay_p95 = $map0["vo_delay_p95_ms"]
        dist1_vo_delay_p95 = $map1["vo_delay_p95_ms"]
        dist0_vo_delay_p99 = $map0["vo_delay_p99_ms"]
        dist1_vo_delay_p99 = $map1["vo_delay_p99_ms"]
    }
}

$summaryCsv = Join-Path $outputDir "analysis_summary.csv"
$rows | Export-Csv -Path $summaryCsv -NoTypeInformation

Write-BarChartSvg -Path (Join-Path $outputDir "throughput_deltas.svg") `
    -Title "One-Link P-EDCA Impact on Throughput" `
    -Rows $rows `
    -Series @(
        @{ Key = "aggregate_delta"; Name = "Aggregate Throughput Delta (Mbit/s)"; Color = "#0f766e" },
        @{ Key = "pedca_vo_avg_delta"; Name = "Avg P-EDCA VO/STA Delta (Mbit/s)"; Color = "#1d4ed8" },
        @{ Key = "edca_vo_avg_delta"; Name = "Avg EDCA VO/STA Delta (Mbit/s)"; Color = "#9333ea" }
    )

Write-BarChartSvg -Path (Join-Path $outputDir "fairness_delay_deltas.svg") `
    -Title "One-Link P-EDCA Impact on Fairness and Delay" `
    -Rows $rows `
    -Series @(
        @{ Key = "jfi_delta"; Name = "JFI Delta"; Color = "#15803d" },
        @{ Key = "vo_delay_p95_delta"; Name = "VO Delay p95 Delta (ms)"; Color = "#dc2626" }
    )

Write-BarChartSvg -Path (Join-Path $outputDir "pedca_efficiency_deltas.svg") `
    -Title "One-Link P-EDCA Impact on P-EDCA Efficiency" `
    -Rows $rows `
    -Series @(
        @{ Key = "pedca_failures_delta"; Name = "P-EDCA Failures Delta"; Color = "#b45309" },
        @{ Key = "pedca_resets_delta"; Name = "P-EDCA Resets Delta"; Color = "#7c3aed" }
    ) `
    -ValueFormat "N0"

Write-HeatmapSvg -Path (Join-Path $outputDir "delay_tails_by_case.svg") `
    -Title "Delay Tail Change by Case" `
    -Rows $rows `
    -Columns @(
        @{ Key = "delay_p95_delta"; Name = "Delay p95" },
        @{ Key = "delay_p99_delta"; Name = "Delay p99" },
        @{ Key = "vo_delay_p95_delta"; Name = "VO Delay p95" },
        @{ Key = "vo_delay_p99_delta"; Name = "VO Delay p99" }
    ) `
    -ValueFormat "N0" `
    -Subtitle "Blue means one-link P-EDCA reduced the delay tail. Orange means the delay tail increased."
Write-HeatmapPng -Path (Join-Path $outputDir "delay_tails_by_case.png") `
    -Title "Delay Tail Change by Case" `
    -Rows $rows `
    -Columns @(
        @{ Key = "delay_p95_delta"; Name = "Delay p95" },
        @{ Key = "delay_p99_delta"; Name = "Delay p99" },
        @{ Key = "vo_delay_p95_delta"; Name = "VO Delay p95" },
        @{ Key = "vo_delay_p99_delta"; Name = "VO Delay p99" }
    ) `
    -ValueFormat "N0" `
    -Subtitle "Blue means one-link P-EDCA reduced the delay tail. Orange means the delay tail increased."

Write-HeatmapSvg -Path (Join-Path $outputDir "throughput_shift_by_case.svg") `
    -Title "Throughput Shift by Case" `
    -Rows $rows `
    -Columns @(
        @{ Key = "aggregate_delta"; Name = "Aggregate" },
        @{ Key = "pedca_throughput_delta"; Name = "P-EDCA" },
        @{ Key = "edca_throughput_delta"; Name = "EDCA" },
        @{ Key = "pedca_vo_avg_delta"; Name = "P-EDCA VO/STA" },
        @{ Key = "edca_vo_avg_delta"; Name = "EDCA VO/STA" }
    ) `
    -ValueFormat "N2" `
    -Subtitle "Blue means the metric went down in one-link mode. Orange means it went up in one-link mode."
Write-HeatmapPng -Path (Join-Path $outputDir "throughput_shift_by_case.png") `
    -Title "Throughput Shift by Case" `
    -Rows $rows `
    -Columns @(
        @{ Key = "aggregate_delta"; Name = "Aggregate" },
        @{ Key = "pedca_throughput_delta"; Name = "P-EDCA" },
        @{ Key = "edca_throughput_delta"; Name = "EDCA" },
        @{ Key = "pedca_vo_avg_delta"; Name = "P-EDCA VO/STA" },
        @{ Key = "edca_vo_avg_delta"; Name = "EDCA VO/STA" }
    ) `
    -ValueFormat "N2" `
    -Subtitle "Blue means the metric went down in one-link mode. Orange means it went up in one-link mode."

Write-DelayComparisonPng -Path (Join-Path $outputDir "delay_tails_absolute_by_case.png") `
    -Title "VO Delay Tails by Case (Dist0 vs Dist1)" `
    -Rows $rows

Write-LineComparisonPng -Path (Join-Path $outputDir "vo_delay_p95_by_case.png") `
    -Title "VO Delay p95 by Client Configuration" `
    -Rows (Get-OrderedRows $rows) `
    -Dist0Key "dist0_vo_delay_p95" `
    -Dist1Key "dist1_vo_delay_p95"

Write-LineComparisonPng -Path (Join-Path $outputDir "vo_delay_p99_by_case.png") `
    -Title "VO Delay p99 by Client Configuration" `
    -Rows (Get-OrderedRows $rows) `
    -Dist0Key "dist0_vo_delay_p99" `
    -Dist1Key "dist1_vo_delay_p99"

Write-LineComparisonPng -Path (Join-Path $outputDir "vo_delay_p95_by_case_clean.png") `
    -Title "VO Delay p95 by Client Configuration" `
    -Rows (Get-OrderedRows $rows) `
    -Dist0Key "dist0_vo_delay_p95" `
    -Dist1Key "dist1_vo_delay_p95"

Write-LineComparisonPng -Path (Join-Path $outputDir "vo_delay_p99_by_case_clean.png") `
    -Title "VO Delay p99 by Client Configuration" `
    -Rows (Get-OrderedRows $rows) `
    -Dist0Key "dist0_vo_delay_p99" `
    -Dist1Key "dist1_vo_delay_p99"

Write-SimpleGroupedBarPng -Path (Join-Path $outputDir "vo_delay_p95_grouped_bar.png") `
    -Title "VO Delay p95 by Client Configuration" `
    -Rows (Get-OrderedRows $rows) `
    -Dist0Key "dist0_vo_delay_p95" `
    -Dist1Key "dist1_vo_delay_p95" `
    -YAxisLabel "VO Delay p95 (ms)"

$positiveFailures = ($rows | Where-Object { $_.pedca_failures_delta -lt 0 } | Measure-Object).Count
$positiveResets = ($rows | Where-Object { $_.pedca_resets_delta -lt 0 } | Measure-Object).Count
$positivePedcaVo = ($rows | Where-Object { $_.pedca_vo_avg_delta -gt 0.05 } | Measure-Object).Count
$negativePedcaVo = ($rows | Where-Object { $_.pedca_vo_avg_delta -lt -0.05 } | Measure-Object).Count
$positiveJfi = ($rows | Where-Object { $_.jfi_delta -gt 0.01 } | Measure-Object).Count
$negativeJfi = ($rows | Where-Object { $_.jfi_delta -lt -0.01 } | Measure-Object).Count
$delayImproved = ($rows | Where-Object { $_.vo_delay_p95_delta -lt -10 } | Measure-Object).Count
$throughputImproved = ($rows | Where-Object { $_.aggregate_delta -gt 0.5 } | Measure-Object).Count
$throughputReduced = ($rows | Where-Object { $_.aggregate_delta -lt -0.5 } | Measure-Object).Count

$selectedRows = $rows | Where-Object { $_.Label -like '*selected*' }

$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Thesis Analysis Summary")
$md.Add("")
$md.Add("Generated from paired Dist0/Dist1 tail-summary.csv files under [results](C:/ns-3/results).")
$md.Add("")
$md.Add("## Visuals")
$md.Add("")
$md.Add("![Throughput deltas](C:/ns-3/results/analysis/throughput_deltas.svg)")
$md.Add("")
$md.Add("![Fairness and delay deltas](C:/ns-3/results/analysis/fairness_delay_deltas.svg)")
$md.Add("")
$md.Add("![P-EDCA efficiency deltas](C:/ns-3/results/analysis/pedca_efficiency_deltas.svg)")
$md.Add("")
$md.Add("![Delay tails by case](C:/ns-3/results/analysis/delay_tails_by_case.svg)")
$md.Add("")
$md.Add("![Throughput shift by case](C:/ns-3/results/analysis/throughput_shift_by_case.svg)")
$md.Add("")
$md.Add("![Absolute VO delay tails by case](C:/ns-3/results/analysis/delay_tails_absolute_by_case.png)")
$md.Add("")
$md.Add("![VO delay p95 by case](C:/ns-3/results/analysis/vo_delay_p95_by_case.png)")
$md.Add("")
$md.Add("![VO delay p99 by case](C:/ns-3/results/analysis/vo_delay_p99_by_case.png)")
$md.Add("")
$md.Add("## Thesis-Style Summary")
$md.Add("")
$md.Add("Restricting P-EDCA to a single balanced link (`Dist1`) has a clear effect on the system, but the effect is workload-dependent rather than uniformly beneficial or harmful. Across the analyzed result pairs, one-link P-EDCA improved P-EDCA procedural efficiency in most runs by reducing P-EDCA failures in $positiveFailures cases and P-EDCA resets in $positiveResets cases. This is the most stable cross-experiment observation.")
$md.Add("")
$md.Add("At the application level, the VO-throughput effect is mixed. One-link P-EDCA increased average P-EDCA VO throughput per STA in $positivePedcaVo cases, but reduced it in $negativePedcaVo cases. The strongest gains appear in low-to-moderate penetration cases such as `2:18`, `3:7`, `5:5`, and `3:27` (`40 MHz, VO=4`). In contrast, higher-share cases such as `9:21` and some `15:15` configurations often show both-links P-EDCA providing the larger raw P-EDCA VO advantage.")
$md.Add("")
$md.Add("Fairness also changes in both directions. JFI improved meaningfully in $positiveJfi runs and worsened meaningfully in $negativeJfi runs. Delay tails, however, often benefited from one-link P-EDCA: VO delay p95 improved by more than `10 ms` in $delayImproved runs. Aggregate throughput increased noticeably in $throughputImproved runs and decreased noticeably in $throughputReduced runs, reinforcing that the one-link policy is best understood as a tradeoff between P-EDCA aggressiveness and contention control rather than a universal improvement.")
$md.Add("")
if ($selectedRows.Count -gt 0) {
    $md.Add("## Selected Thesis Cases")
    $md.Add("")
    foreach ($row in $selectedRows) {
        $aggText = [string]::Format('{0:N2}', $row.aggregate_delta)
        $pedcaVoText = [string]::Format('{0:N3}', $row.pedca_vo_avg_delta)
        $edcaVoText = [string]::Format('{0:N3}', $row.edca_vo_avg_delta)
        $jfiText = [string]::Format('{0:N3}', $row.jfi_delta)
        $md.Add("- $($row.Label): aggregate delta $aggText Mbit/s, P-EDCA VO/STA delta $pedcaVoText Mbit/s, EDCA VO/STA delta $edcaVoText Mbit/s, JFI delta $jfiText.")
    }
    $md.Add("")
}
$md.Add("## Files")
$md.Add("")
$md.Add("- [analysis_summary.csv](C:/ns-3/results/analysis/analysis_summary.csv)")
$md.Add("- [throughput_deltas.svg](C:/ns-3/results/analysis/throughput_deltas.svg)")
$md.Add("- [fairness_delay_deltas.svg](C:/ns-3/results/analysis/fairness_delay_deltas.svg)")
$md.Add("- [pedca_efficiency_deltas.svg](C:/ns-3/results/analysis/pedca_efficiency_deltas.svg)")
$md.Add("- [delay_tails_by_case.svg](C:/ns-3/results/analysis/delay_tails_by_case.svg)")
$md.Add("- [delay_tails_by_case.png](C:/ns-3/results/analysis/delay_tails_by_case.png)")
$md.Add("- [throughput_shift_by_case.svg](C:/ns-3/results/analysis/throughput_shift_by_case.svg)")
$md.Add("- [throughput_shift_by_case.png](C:/ns-3/results/analysis/throughput_shift_by_case.png)")
$md.Add("- [delay_tails_absolute_by_case.png](C:/ns-3/results/analysis/delay_tails_absolute_by_case.png)")
$md.Add("- [vo_delay_p95_by_case.png](C:/ns-3/results/analysis/vo_delay_p95_by_case.png)")
$md.Add("- [vo_delay_p99_by_case.png](C:/ns-3/results/analysis/vo_delay_p99_by_case.png)")
$md.Add("- [vo_delay_p95_by_case_clean.png](C:/ns-3/results/analysis/vo_delay_p95_by_case_clean.png)")
$md.Add("- [vo_delay_p99_by_case_clean.png](C:/ns-3/results/analysis/vo_delay_p99_by_case_clean.png)")
$md.Add("- [vo_delay_p95_grouped_bar.png](C:/ns-3/results/analysis/vo_delay_p95_grouped_bar.png)")

$summaryMd = Join-Path $outputDir "thesis_summary.md"
Set-Content -Path $summaryMd -Value $md

Write-Output "Created:"
Write-Output $summaryCsv
Write-Output (Join-Path $outputDir "throughput_deltas.svg")
Write-Output (Join-Path $outputDir "fairness_delay_deltas.svg")
Write-Output (Join-Path $outputDir "pedca_efficiency_deltas.svg")
Write-Output (Join-Path $outputDir "delay_tails_by_case.svg")
Write-Output (Join-Path $outputDir "delay_tails_by_case.png")
Write-Output (Join-Path $outputDir "throughput_shift_by_case.svg")
Write-Output (Join-Path $outputDir "throughput_shift_by_case.png")
Write-Output (Join-Path $outputDir "delay_tails_absolute_by_case.png")
Write-Output (Join-Path $outputDir "vo_delay_p95_by_case.png")
Write-Output (Join-Path $outputDir "vo_delay_p99_by_case.png")
Write-Output (Join-Path $outputDir "vo_delay_p95_by_case_clean.png")
Write-Output (Join-Path $outputDir "vo_delay_p99_by_case_clean.png")
Write-Output (Join-Path $outputDir "vo_delay_p95_grouped_bar.png")
Write-Output $summaryMd
