// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_card.h"

#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/global_media_controls/public/media_item_manager.h"
#include "components/global_media_controls/public/media_dialog_delegate.h"
#include "components/media_message_center/media_notification_item.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

class FakeMediaItemManager : public global_media_controls::MediaItemManager {
 public:
  FakeMediaItemManager() = default;
  ~FakeMediaItemManager() override = default;

  void AddObserver(global_media_controls::MediaItemManagerObserver* observer) override {}
  void RemoveObserver(global_media_controls::MediaItemManagerObserver* observer) override {}
  void AddItemProducer(global_media_controls::MediaItemProducer* producer) override {}
  void RemoveItemProducer(global_media_controls::MediaItemProducer* producer) override {}
  void ShowItem(const std::string& id) override {}
  void HideItem(const std::string& id) override {}
  void RefreshItem(const std::string& id) override {}
  void OnItemsChanged() override {}
  void SetDialogDelegate(global_media_controls::MediaDialogDelegate* delegate) override {
    delegate_transitions_.push_back(delegate);
    delegate_ = delegate;
  }
  void SetDialogDelegateForId(global_media_controls::MediaDialogDelegate* delegate,
                              const std::string& id) override {}
  void FocusDialog() override {}
  void HideDialog() override {}
  bool HasActiveItems() override { return false; }
  bool HasFrozenItems() override { return false; }
  bool HasOpenDialog() override { return delegate_ != nullptr; }
  std::list<std::string> GetActiveItemIds() override { return {}; }
  base::WeakPtr<MediaItemManager> GetWeakPtr() override { return weak_factory_.GetWeakPtr(); }

  global_media_controls::MediaDialogDelegate* delegate_ = nullptr;
  std::vector<global_media_controls::MediaDialogDelegate*> delegate_transitions_;

 private:
  base::WeakPtrFactory<FakeMediaItemManager> weak_factory_{this};
};

class FakeMediaNotificationItem : public media_message_center::MediaNotificationItem {
 public:
  FakeMediaNotificationItem() = default;
  ~FakeMediaNotificationItem() override = default;

  void SetView(media_message_center::MediaNotificationView* view) override {}
  void OnMediaSessionActionButtonPressed(media_session::mojom::MediaSessionAction action) override {}
  void SeekTo(base::TimeDelta time) override {}
  void Dismiss() override {}
  void SetVolume(float volume) override {}
  void SetMute(bool mute) override {}
  bool RequestMediaRemoting() override { return false; }
  media_message_center::Source GetSource() const override { return media_message_center::Source::kWeb; }
  media_message_center::SourceType GetSourceType() const override { return media_message_center::SourceType::kLocalMediaSession; }
  std::optional<base::UnguessableToken> GetSourceId() const override { return std::nullopt; }

  base::WeakPtr<FakeMediaNotificationItem> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  base::WeakPtrFactory<FakeMediaNotificationItem> weak_factory_{this};
};

class FakeMahoNowPlayingCardHost : public MahoNowPlayingCardHost {
 public:
  explicit FakeMahoNowPlayingCardHost(Browser* browser) : browser_(browser) {}
  ~FakeMahoNowPlayingCardHost() override = default;

  Browser* GetBrowser() override { return browser_; }
  global_media_controls::MediaItemUI* ShowMediaItem(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) override {
    current_item_id_ = id;
    show_called_ = true;
    return nullptr;
  }
  void HideMediaItem(const std::string& id) override {
    if (current_item_id_ == id) {
      current_item_id_.clear();
    }
    hide_called_ = true;
  }
  void RefreshMediaItem(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) override {
    refresh_called_ = true;
  }
  void HideMediaDialog() override {
    dialog_hidden_ = true;
  }
  void Focus() override {
    focus_called_ = true;
  }

  std::unique_ptr<MahoNowPlayingCard> TakeCard() override {
    has_card_ = false;
    card_taken_ = true;
    return nullptr;
  }
  void AdoptCard(std::unique_ptr<MahoNowPlayingCard> card,
                 base::WeakPtr<media_message_center::MediaNotificationItem> item,
                 const std::string& id) override {
    has_card_ = true;
    current_item_id_ = id;
    card_adopted_ = true;
  }
  void SetTrackedItemState(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) override {
    current_item_id_ = id;
  }
  bool HasCard() const override { return has_card_; }
  std::string GetCurrentItemId() const override { return current_item_id_; }

  std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> TakeActiveItems() override {
    return active_items_;
  }
  void SetActiveItems(
      std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> items) override {
    active_items_ = items;
  }

  raw_ptr<Browser> browser_;
  std::string current_item_id_;
  bool has_card_ = false;
  bool show_called_ = false;
  bool hide_called_ = false;
  bool refresh_called_ = false;
  bool dialog_hidden_ = false;
  bool focus_called_ = false;
  bool card_taken_ = false;
  bool card_adopted_ = false;
  std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> active_items_;
};

class MahoNowPlayingCoordinatorTest : public BrowserWithTestWindowTest {
 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    fake_manager_ = std::make_unique<FakeMediaItemManager>();
    coordinator_ = std::make_unique<MahoNowPlayingCoordinator>(profile(), fake_manager_.get());
  }

  void TearDown() override {
    coordinator_.reset();
    fake_manager_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  std::unique_ptr<FakeMediaItemManager> fake_manager_;
  std::unique_ptr<MahoNowPlayingCoordinator> coordinator_;
};

TEST_F(MahoNowPlayingCoordinatorTest, DelegateLifecycle) {
  // Initially, manager has no delegate.
  EXPECT_EQ(nullptr, fake_manager_->delegate_);
  EXPECT_TRUE(fake_manager_->delegate_transitions_.empty());

  // First host registers -> coordinator registers as manager delegate.
  std::unique_ptr<BrowserWindow> window2 = CreateBrowserWindow();
  std::unique_ptr<Browser> browser2 = CreateBrowser(
      profile(), Browser::Type::TYPE_NORMAL, false, window2.release());

  FakeMahoNowPlayingCardHost host1(browser());
  coordinator_->RegisterHost(&host1);

  EXPECT_EQ(coordinator_.get(), fake_manager_->delegate_);
  ASSERT_EQ(1u, fake_manager_->delegate_transitions_.size());
  EXPECT_EQ(coordinator_.get(), fake_manager_->delegate_transitions_[0]);

  // Second host registers -> no transition change.
  FakeMahoNowPlayingCardHost host2(browser2.get());
  coordinator_->RegisterHost(&host2);

  EXPECT_EQ(coordinator_.get(), fake_manager_->delegate_);
  EXPECT_EQ(1u, fake_manager_->delegate_transitions_.size());

  // Unregister host 1 -> remains delegate because host 2 exists.
  coordinator_->UnregisterHost(&host1);

  EXPECT_EQ(coordinator_.get(), fake_manager_->delegate_);
  EXPECT_EQ(1u, fake_manager_->delegate_transitions_.size());

  // Unregister host 2 -> manager delegate cleared.
  coordinator_->UnregisterHost(&host2);

  EXPECT_EQ(nullptr, fake_manager_->delegate_);
  ASSERT_EQ(2u, fake_manager_->delegate_transitions_.size());
  EXPECT_EQ(nullptr, fake_manager_->delegate_transitions_[1]);
}

TEST_F(MahoNowPlayingCoordinatorTest, FallbackRouting) {
  FakeMahoNowPlayingCardHost host1(browser());
  coordinator_->RegisterHost(&host1);

  FakeMediaNotificationItem item;

  // Fallback to active/only host when no WebContents is associated.
  coordinator_->ShowMediaItem("item1", item.GetWeakPtr());
  EXPECT_TRUE(host1.show_called_);
  EXPECT_EQ("item1", host1.GetCurrentItemId());

  coordinator_->HideMediaItem("item1");
  EXPECT_TRUE(host1.hide_called_);
  EXPECT_EQ("", host1.GetCurrentItemId());
}

}  // namespace maho
