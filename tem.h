// 在 windowProc 中用固定偏移计算包围盒
static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 若正在拖拽，优先处理拖拽逻辑
    if (g_isDragging)
    {
        switch (msg)
        {
        case WM_MOUSEMOVE:
        {
            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);

            // 计算矩形新位置（相对于画布客户区像素）
            float newX = (float)(mouseX - g_dragOffsetX - ogv.canvasPosX);
            float newY = (float)(mouseY - g_dragOffsetY - ogv.canvasPosY);

            // 边界限制
            if (newX < 0) newX = 0;
            if (newY < 0) newY = 0;
            if (newX + g_rectProps.width > ogv.canvasWidth)
                newX = ogv.canvasWidth - g_rectProps.width;
            if (newY + g_rectProps.height > ogv.canvasHeight)
                newY = ogv.canvasHeight - g_rectProps.height;

            if (newX != g_rectProps.x || newY != g_rectProps.y)
            {
                g_rectProps.x = newX;
                g_rectProps.y = newY;
                updateRectangle();
                renderThorVG();
            }

            ogv.mouseX = (uint32_t)mouseX;
            ogv.mouseY = (uint32_t)mouseY;

            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            g_isDragging = false;
            ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                g_isDragging = false;
                ReleaseCapture();
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return 0;
            }
            break;
        }

        // 拖拽期间屏蔽 Nuklear 鼠标事件
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
            msg == WM_MOUSEMOVE || msg == WM_LBUTTONDBLCLK)
            return 0;
    }

    // 非拖拽状态下的鼠标检测
    switch (msg)
    {
    case WM_MOUSEMOVE:
    {
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);
        ogv.mouseX = (uint32_t)mouseX;
        ogv.mouseY = (uint32_t)mouseY;

        // 矩形在客户区里的像素包围盒
        float rectLeft = ogv.canvasPosX + g_rectProps.x;
        float rectTop = ogv.canvasPosY + g_rectProps.y;
        float rectRight = rectLeft + g_rectProps.width;
        float rectBottom = rectTop + g_rectProps.height;

        if (mouseX >= rectLeft && mouseX <= rectRight &&
            mouseY >= rectTop && mouseY <= rectBottom)
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        else
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        break;
    }

    case WM_LBUTTONDOWN:
    {
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);

        float rectLeft = ogv.canvasPosX + g_rectProps.x;
        float rectTop = ogv.canvasPosY + g_rectProps.y;
        float rectRight = rectLeft + g_rectProps.width;
        float rectBottom = rectTop + g_rectProps.height;

        if (mouseX >= rectLeft && mouseX <= rectRight &&
            mouseY >= rectTop && mouseY <= rectBottom)
        {
            g_isDragging = true;
            g_dragOffsetX = mouseX - rectLeft;
            g_dragOffsetY = mouseY - rectTop;
            SetCapture(hwnd);
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            return 0;
        }
        break;
    }
    }

    // 让 Nuklear 处理其余事件
    if (nk_gdi_handle_event(hwnd, msg, wParam, lParam))
        return 0;

    switch (msg)
    {
    case WM_SIZE:
    {
        unsigned width = LOWORD(lParam);
        unsigned height = HIWORD(lParam);
        if (width != ogv.windowWidth || height != ogv.windowheight)
        {
            ogv.windowWidth = width;
            ogv.windowheight = height;
        }
        return 0;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}