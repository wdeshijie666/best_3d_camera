/**
 * @file RawImageAlbumPage.cpp
 * @brief 原始图像相册页：ElaScrollArea + ElaPushButton 缩略图条
 */
#include "RawImageAlbumPage.h"
#include "common/unified_image_qt.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <ElaPushButton.h>
#include <ElaScrollArea.h>

namespace scanner_viewer {

namespace {
constexpr int kThumbPx = 72;
constexpr int kIconPx = 64;
}  // namespace

RawImageAlbumPage::RawImageAlbumPage(QWidget* parent) : QWidget(parent) {
    auto* main = new QVBoxLayout(this);
    main->setContentsMargins(8, 8, 8, 8);
    main->setSpacing(8);

    preview_ = new QLabel(this);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(240);
    preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    preview_->setStyleSheet(
        "QLabel { background-color: #FFFFFF; border: 1px solid #B3D9F2; border-radius: 6px; "
        "color: #5A7A99; padding: 12px; }");

    caption_ = new QLabel(this);
    caption_->setStyleSheet("color: #2C5282; font-size: 12px;");
    caption_->setText(QString());

    thumb_scroll_ = new ElaScrollArea(this);
    thumb_scroll_->setWidgetResizable(true);
    thumb_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    thumb_scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    thumb_scroll_->setFixedHeight(kThumbPx + 28);
    thumb_scroll_->setFrameShape(QFrame::NoFrame);

    thumb_inner_ = new QWidget(thumb_scroll_);
    thumb_layout_ = new QHBoxLayout(thumb_inner_);
    thumb_layout_->setContentsMargins(4, 4, 4, 4);
    thumb_layout_->setSpacing(8);
    thumb_layout_->addStretch(1);
    thumb_scroll_->setWidget(thumb_inner_);

    thumb_group_ = new QButtonGroup(this);
    thumb_group_->setExclusive(true);

    main->addWidget(preview_, 1);
    main->addWidget(caption_);
    main->addWidget(thumb_scroll_);

    connect(thumb_group_, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this,
            [this](QAbstractButton* btn) {
                const int id = thumb_group_->id(btn);
                if (id >= 0)
                    SetSelectedIndex(id);
            });
}

void RawImageAlbumPage::Clear() {
    images_.clear();
    selected_ = 0;
    ClearThumbnails();
    preview_->clear();
    preview_->setPixmap(QPixmap());
    preview_->setText(tr(u8"暂无原始图像组数据。\n请连接设备并完成含多帧原始图的采集。"));
    caption_->clear();
    thumb_scroll_->setVisible(false);
}

void RawImageAlbumPage::ClearThumbnails() {
    const QList<QAbstractButton*> buttons = thumb_group_->buttons();
    for (QAbstractButton* b : buttons) {
        thumb_group_->removeButton(b);
        delete b;
    }
}

void RawImageAlbumPage::RebuildFromRaws(const std::vector<UnifiedImage>& raws) {
    ClearThumbnails();
    images_.clear();
    images_.reserve(raws.size());
    for (const UnifiedImage& u : raws) {
        QImage q = UnifiedImageToQImage(u);
        if (!q.isNull())
            images_.push_back(std::move(q));
    }
    if (images_.empty()) {
        Clear();
        return;
    }

    selected_ = 0;
    thumb_scroll_->setVisible(true);

    for (int i = 0; i < static_cast<int>(images_.size()); ++i) {
        auto* btn = new ElaPushButton(QString::number(i + 1), thumb_inner_);
        btn->setCheckable(true);
        btn->setToolTip(tr(u8"第 %1 张").arg(i + 1));
        const QPixmap icon =
            QPixmap::fromImage(images_[i].scaled(kIconPx, kIconPx, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        btn->setIcon(QIcon(icon));
        btn->setIconSize(QSize(kIconPx, kIconPx));
        btn->setFixedSize(kThumbPx, kThumbPx);
        thumb_group_->addButton(btn, i);
        thumb_layout_->insertWidget(thumb_layout_->count() - 1, btn);
        if (i == 0)
            btn->setChecked(true);
    }

    const int n = static_cast<int>(images_.size());
    thumb_inner_->setMinimumWidth(qMax(200, n * (kThumbPx + thumb_layout_->spacing()) + 24));

    caption_->setText(tr(u8"共 %1 张原始图，点击下方缩略图切换").arg(n));
    UpdateLargePreview();
}

void RawImageAlbumPage::SetFrame(const UnifiedFrame& frame) {
    if (frame.hardware_raw_frames.empty()) {
        if (!images_.empty())
            Clear();
        return;
    }
    RebuildFromRaws(frame.hardware_raw_frames);
}

void RawImageAlbumPage::SetSelectedIndex(int index) {
    if (images_.empty() || index < 0 || index >= static_cast<int>(images_.size()))
        return;
    selected_ = index;
    UpdateLargePreview();
}

void RawImageAlbumPage::UpdateLargePreview() {
    if (images_.empty() || selected_ < 0 || selected_ >= static_cast<int>(images_.size())) {
        preview_->clear();
        return;
    }
    preview_->setText(QString());
    const QSize box = preview_->contentsRect().size();
    if (box.width() < 8 || box.height() < 8)
        return;
    const QImage scaled =
        images_[selected_].scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    preview_->setPixmap(QPixmap::fromImage(scaled));
    preview_->setAlignment(Qt::AlignCenter);
}

void RawImageAlbumPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!images_.empty())
        UpdateLargePreview();
}

void RawImageAlbumPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!images_.empty())
        UpdateLargePreview();
}

}  // namespace scanner_viewer
