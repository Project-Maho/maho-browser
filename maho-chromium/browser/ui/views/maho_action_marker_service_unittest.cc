// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_action_marker_service.h"

#include <optional>

#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoActionMarkerServiceTest, LifetimeIsBoundedToSevenHundredMilliseconds) {
  EXPECT_EQ(MahoActionMarkerService::kMarkerLifetime,
            base::Milliseconds(700));
}

TEST(MahoActionMarkerServiceTest, ReadingNeverGetsSpatialMarker) {
  EXPECT_FALSE(MahoActionMarkerService::OperationGetsSpatialMarker(
      /*is_reading=*/true, MahoActionMarkerService::Kind::kClick));
  EXPECT_FALSE(MahoActionMarkerService::OperationGetsSpatialMarker(
      /*is_reading=*/true, std::nullopt));
}

TEST(MahoActionMarkerServiceTest, OnlyAttributedActionsGetSpatialMarkers) {
  EXPECT_TRUE(MahoActionMarkerService::OperationGetsSpatialMarker(
      /*is_reading=*/false, MahoActionMarkerService::Kind::kClick));
  EXPECT_TRUE(MahoActionMarkerService::OperationGetsSpatialMarker(
      /*is_reading=*/false, MahoActionMarkerService::Kind::kHover));
  EXPECT_TRUE(MahoActionMarkerService::OperationGetsSpatialMarker(
      /*is_reading=*/false, MahoActionMarkerService::Kind::kFocus));
  EXPECT_FALSE(MahoActionMarkerService::OperationGetsSpatialMarker(
      /*is_reading=*/false, std::nullopt));
}

TEST(MahoActionMarkerServiceTest, GeometryCentersAndClampsToContents) {
  EXPECT_EQ(MahoActionMarkerService::ComputeMarkerBoundsForTesting(
                gfx::Size(800, 600), gfx::PointF(200.0f, 150.0f),
                /*rich_motion=*/true),
            gfx::Rect(180, 130, 40, 40));
  EXPECT_EQ(MahoActionMarkerService::ComputeMarkerBoundsForTesting(
                gfx::Size(100, 100), gfx::PointF(3.0f, 97.0f),
                /*rich_motion=*/false),
            gfx::Rect(0, 72, 28, 28));
  EXPECT_TRUE(MahoActionMarkerService::ComputeMarkerBoundsForTesting(
                  gfx::Size(), gfx::PointF(3.0f, 3.0f),
                  /*rich_motion=*/true)
                  .IsEmpty());
}

TEST(MahoActionMarkerServiceTest, RequestCarriesNoPageContent) {
  MahoActionMarkerService::Request request;
  request.viewport_point = gfx::PointF(12.5f, 24.5f);
  request.kind = MahoActionMarkerService::Kind::kFocus;
  request.sensitive = true;

  EXPECT_EQ(request.viewport_point, gfx::PointF(12.5f, 24.5f));
  EXPECT_EQ(request.kind, MahoActionMarkerService::Kind::kFocus);
  EXPECT_TRUE(request.sensitive);
  EXPECT_FALSE(request.primary_frame_id);
}

}  // namespace maho
