/**
 * @file RawImageAlbumPage.h
 * @brief 「原始图像」页：上方大图预览，底部横向缩略图条（相册式选帧）
 */
#ifndef SCANNER_VIEWER_RAW_IMAGE_ALBUM_PAGE_H
#define SCANNER_VIEWER_RAW_IMAGE_ALBUM_PAGE_H

#include <QWidget>
#include <vector>

#include "model/data_center/UnifiedFrame.h"

class QLabel;
class QHBoxLayout;
class QButtonGroup;
class QResizeEvent;
class QShowEvent;
class ElaScrollArea;

namespace scanner_viewer {

/** 展示 hardware_raw_frames 组：底部 Ela 风格缩略图，上方当前选中大图 */
class RawImageAlbumPage : public QWidget {
public:
    explicit RawImageAlbumPage(QWidget* parent = nullptr);
    void SetFrame(const UnifiedFrame& frame);
    void Clear();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void ClearThumbnails();
    void RebuildFromRaws(const std::vector<UnifiedImage>& raws);
    void SetSelectedIndex(int index);
    void UpdateLargePreview();

    QLabel* preview_{nullptr};
    QLabel* caption_{nullptr};
    ElaScrollArea* thumb_scroll_{nullptr};
    QWidget* thumb_inner_{nullptr};
    QHBoxLayout* thumb_layout_{nullptr};
    QButtonGroup* thumb_group_{nullptr};
    std::vector<QImage> images_;
    int selected_{0};
};

}  // namespace scanner_viewer

#endif
