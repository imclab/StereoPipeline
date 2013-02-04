// __BEGIN_LICENSE__
//  Copyright (c) 2009-2012, United States Government as represented by the
//  Administrator of the National Aeronautics and Space Administration. All
//  rights reserved.
//
//  The NGT platform is licensed under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance with the
//  License. You may obtain a copy of the License at
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// __END_LICENSE__


#ifndef __ASP_CORE_INTEREST_POINT_MATCHING_H__
#define __ASP_CORE_INTEREST_POINT_MATCHING_H__

#include <asp/Core/IntegralAutoGainDetector.h>

#include <vw/Core.h>
#include <vw/Math.h>
#include <vw/Image/ImageViewBase.h>
#include <vw/Image/MaskViews.h>
#include <vw/Camera/CameraModel.h>
#include <vw/InterestPoint/Matcher.h>
#include <vw/InterestPoint/InterestData.h>
#include <vw/Cartography/Datum.h>

#include <boost/foreach.hpp>
#include <boost/math/special_functions/fpclassify.hpp>

namespace asp {

  // Takes interest points and then finds the nearest 10 matches. It
  // filters them by whom are closest to the epipolar line via a
  // threshold. The remaining 2 or then selected to be a match if
  // their distance meets the other threshold.
  class EpipolarLinePointMatcher {
    double m_threshold, m_epipolar_threshold;
    vw::cartography::Datum m_datum;

  public:
    EpipolarLinePointMatcher( double threshold, double epipolar_threshold,
                              vw::cartography::Datum const& datum ) :
      m_threshold(threshold), m_epipolar_threshold(epipolar_threshold), m_datum(datum) {}

    // This only returns the indicies
    void operator()( std::vector<vw::ip::InterestPoint> const& ip1,
                     std::vector<vw::ip::InterestPoint> const& ip2,
                     vw::camera::CameraModel* cam1,
                     vw::camera::CameraModel* cam2,
                     vw::TransformRef const& tx1,
                     vw::TransformRef const& tx2,
                     std::vector<size_t>& output_indices,
                     const vw::ProgressCallback &progress_callback = vw::ProgressCallback::dummy_instance() ) const;

    // Work out an epipolar line from interest point. Returns the
    // coefficients for the following line equation: ax + by + c = 0
    static vw::Vector3 epipolar_line( vw::Vector2 const& feature,
                               vw::cartography::Datum const& datum,
                               vw::camera::CameraModel* cam_ip,
                                      vw::camera::CameraModel* cam_obj );

    // Calculate distance between a line of equation ax + by + c = 0
    static double distance_point_line( vw::Vector3 const& line,
                                vw::Vector2 const& point );
  };

  // Tool to remove points on or within 1 px of nodata
  template <class ImageT>
  void remove_ip_near_nodata( vw::ImageViewBase<ImageT> const& image_base,
                              double nodata,
                              vw::ip::InterestPointList& ip_list ){
    using namespace vw;
    ImageT image = image_base.impl();
    size_t prior_ip = ip_list.size();

    typedef ImageView<typename ImageT::pixel_type> CropImageT;
    CropImageT subsection(3,3);

    BBox2i bound = bounding_box( image );
    bound.contract(1);
    for ( ip::InterestPointList::iterator ip = ip_list.begin();
          ip != ip_list.end(); ++ip ) {
      if ( !bound.contains( Vector2i(ip->ix,ip->iy) ) ) {
        ip = ip_list.erase(ip);
        ip--;
        continue;
      }

      subsection =
        crop( image, ip->ix-1, ip->iy-1, 3, 3 );
      for ( typename CropImageT::iterator pixel = subsection.begin();
            pixel != subsection.end(); pixel++ ) {
        if (*pixel == nodata) {
          ip = ip_list.erase(ip);
          ip--;
          break;
        }
      }
    }
    VW_OUT( DebugMessage, "asp" ) << "Removed " << prior_ip - ip_list.size()
                                  << " interest points due to their proximity to nodata values."
                                  << std::endl << "Nodata value used "
                                  << nodata << std::endl;
  }

  // Find a rough homography that maps right to left using the camera
  // and datum information.
  vw::Matrix<double>
  rough_homography_fit( vw::camera::CameraModel* cam1,
                        vw::camera::CameraModel* cam2,
                        vw::BBox2i const& box1, vw::BBox2i const& box2,
                        vw::cartography::Datum const& datum );

  // Homography fit to interest points
  vw::Matrix<double>
  homography_fit( std::vector<vw::ip::InterestPoint> const& ip1,
                  std::vector<vw::ip::InterestPoint> const& ip2,
                  vw::BBox2i const& image_size );


  // Detect InterestPoints
  //
  // This is not meant to be used directly. Please use ip_matching or
  // the dumb homography ip matching.
  template <class List1T, class List2T, class Image1T, class Image2T>
  void detect_ip( List1T& ip1, List2T& ip2,
                  vw::ImageViewBase<Image1T> const& image1_base,
                  vw::ImageViewBase<Image2T> const& image2_base,
                  double nodata1 = std::numeric_limits<double>::quiet_NaN(),
                  double nodata2 = std::numeric_limits<double>::quiet_NaN() ) {
    using namespace vw;
    Image1T image1 = image1_base.impl();
    Image2T image2 = image2_base.impl();
    BBox2i box1 = bounding_box(image1);
    ip1.clear();
    ip2.clear();

    // Detect Interest Points
    float number_boxes = (box1.width() / 1024.f) * (box1.height() / 1024.f);
    size_t points_per_tile = 5000.f / number_boxes;
    if ( points_per_tile > 5000 ) points_per_tile = 5000;
    if ( points_per_tile < 50 ) points_per_tile = 50;
    VW_OUT( DebugMessage, "asp" ) << "Setting IP code to search " << points_per_tile << " IP per tile (1024^2 px).\n";
    asp::IntegralAutoGainDetector detector( points_per_tile );
    vw_out() << "\t    Processing Left" << std::endl;
    if ( boost::math::isnan(nodata1) )
      ip1 = detect_interest_points( image1, detector );
    else
      ip1 = detect_interest_points( apply_mask(create_mask(image1,nodata1)), detector );
    vw_out() << "\t    Processing Right" << std::endl;
    if ( boost::math::isnan(nodata2) )
      ip2 = detect_interest_points( image2, detector );
    else
      ip2 = detect_interest_points( apply_mask(create_mask(image2,nodata2)), detector );

    if ( !boost::math::isnan(nodata1) )
      remove_ip_near_nodata( image1, nodata1, ip1 );

    if ( !boost::math::isnan(nodata2) )
      remove_ip_near_nodata( image2, nodata2, ip2 );

    vw_out() << "\t    Building Descriptors" << std::endl;
    ip::SGradDescriptorGenerator descriptor;
    if ( boost::math::isnan(nodata1) )
      descriptor(image1, ip1 );
    else
      descriptor( apply_mask(create_mask(image1,nodata1)), ip1 );
    if ( boost::math::isnan(nodata2) )
      descriptor(image2, ip2 );
    else
      descriptor( apply_mask(create_mask(image2,nodata2)), ip2 );

    vw_out() << "\t    Found interest points:\n"
             << "\t      left: " << ip1.size() << "\n";
    vw_out() << "\t     right: " << ip2.size() << std::endl;
  }

  // Detect and Match Interest Points
  //
  // This is not meant to be used directly. Please use ip matching
  template <class Image1T, class Image2T>
  void detect_match_ip( std::vector<vw::ip::InterestPoint>& matched_ip1,
                        std::vector<vw::ip::InterestPoint>& matched_ip2,
                        vw::ImageViewBase<Image1T> const& image1_base,
                        vw::ImageViewBase<Image2T> const& image2_base,
                        double nodata1 = std::numeric_limits<double>::quiet_NaN(),
                        double nodata2 = std::numeric_limits<double>::quiet_NaN() ) {
    using namespace vw;

    // Detect Interest Points
    ip::InterestPointList ip1, ip2;
    detect_ip( ip1, ip2, image1_base.impl(),
               image2_base.impl(),
               nodata1, nodata2 );

    // Match the interset points using the default matcher
    vw_out() << "\t--> Matching interest points\n";
    ip::InterestPointMatcher<ip::L2NormMetric,ip::NullConstraint> matcher(0.5);

    // Copy to vector for random access iterators and for sorting so
    // we get the same results.
    std::vector<ip::InterestPoint> ip1_copy, ip2_copy;
    ip::sort_interest_points( ip1, ip2, ip1_copy, ip2_copy );

    matcher( ip1_copy, ip2_copy, matched_ip1, matched_ip2,
             TerminalProgressCallback( "asp", "\t   Matching: " ));
    ip::remove_duplicates( matched_ip1, matched_ip2 );
    vw_out() << "\t    Matched points: " << matched_ip1.size() << std::endl;
  }

  // Homography IP matching
  //
  // This applies only the homography constraint. Not the best...
  template <class Image1T, class Image2T>
  bool homography_ip_matching( vw::ImageViewBase<Image1T> const& image1_base,
                               vw::ImageViewBase<Image2T> const& image2_base,
                               std::string const& output_name,
                               double nodata1 = std::numeric_limits<double>::quiet_NaN(),
                               double nodata2 = std::numeric_limits<double>::quiet_NaN() ) {
    using namespace vw;

    std::vector<ip::InterestPoint> matched_ip1, matched_ip2;
    detect_match_ip( matched_ip1, matched_ip2,
                     image1_base.impl(), image2_base.impl(),
                     nodata1, nodata2 );
    if ( matched_ip1.size() == 0 || matched_ip2.size() == 0 )
      return false;
    std::vector<Vector3> ransac_ip1 = iplist_to_vectorlist(matched_ip1),
      ransac_ip2 = iplist_to_vectorlist(matched_ip2);
    std::vector<size_t> indices;
    try {
      typedef math::RandomSampleConsensus<math::HomographyFittingFunctor,
                                          math::InterestPointErrorMetric> RansacT;
      RansacT ransac( math::HomographyFittingFunctor(),
                      math::InterestPointErrorMetric(), 100,
                      norm_2(Vector2(bounding_box(image1_base.impl()).size()))/100.0,
                      ransac_ip1.size()/2, true
                      );
      Matrix<double> H(ransac(ransac_ip1,ransac_ip2));
      vw_out() << "\t--> Homography: " << H << "\n";
      indices = ransac.inlier_indices(H,ransac_ip1,ransac_ip2);
    } catch (const vw::math::RANSACErr& e ) {
      vw_out() << "RANSAC Failed: " << e.what() << "\n";
      return false;
    }

    std::vector<ip::InterestPoint> final_ip1, final_ip2;
    BOOST_FOREACH( size_t& index, indices ) {
      final_ip1.push_back(matched_ip1[index]);
      final_ip2.push_back(matched_ip2[index]);
    }

    ip::write_binary_match_file(output_name, final_ip1, final_ip2);
    return true;
  }

  // Smart IP matching that uses clustering on triangulation error and
  // altitude to determine inliers.
  //
  // Check output this filter can fail. Output is the index list.
  bool
  tri_and_alt_ip_filtering( std::vector<vw::ip::InterestPoint> const& matched_ip1,
                            std::vector<vw::ip::InterestPoint> const& matched_ip2,
                            vw::camera::CameraModel* cam1,
                            vw::camera::CameraModel* cam2,
                            vw::cartography::Datum const& datum,
                            std::list<size_t>& output,
                            vw::TransformRef const& left_tx = vw::TransformRef(vw::TranslateTransform(0,0)),
                            vw::TransformRef const& right_tx = vw::TransformRef(vw::TranslateTransform(0,0)) );

  // Smart IP matching that uses clustering on triangulation and
  // datum information to determine inliers.
  //
  // Left and Right TX define transforms that have been performed on
  // the images that that camera data doesn't know about. (ie
  // scaling).
  template <class Image1T, class Image2T>
  bool ip_matching( vw::camera::CameraModel* cam1,
                    vw::camera::CameraModel* cam2,
                    vw::ImageViewBase<Image1T> const& image1_base,
                    vw::ImageViewBase<Image2T> const& image2_base,
                    vw::cartography::Datum const& datum,
                    std::string const& output_name,
                    double nodata1 = std::numeric_limits<double>::quiet_NaN(),
                    double nodata2 = std::numeric_limits<double>::quiet_NaN(),
                    vw::TransformRef const& left_tx = vw::TransformRef(vw::TranslateTransform(0,0)),
                    vw::TransformRef const& right_tx = vw::TransformRef(vw::TranslateTransform(0,0)),
                    bool transform_to_original_coord = true ) {
    using namespace vw;

    Image1T image1 = image1_base.impl();
    Image2T image2 = image2_base.impl();

    // Detect interest points
    ip::InterestPointList ip1, ip2;
    detect_ip( ip1, ip2, image1, image2,
               nodata1, nodata2 );
    if ( ip1.size() == 0 || ip2.size() == 0 )
      return false;

    // Convert over to vector for random access iterators
    std::vector<ip::InterestPoint> ip1_copy, ip2_copy;
    ip::sort_interest_points( ip1, ip2, ip1_copy, ip2_copy );
    ip1.clear(); ip2.clear();

    // Match interest points forward/backward .. constraining on epipolar line
    std::vector<size_t> forward_match, backward_match;
    vw_out() << "\t--> Matching interest points" << std::endl;
    EpipolarLinePointMatcher matcher( 0.5, norm_2(Vector2(image1.cols(),image1.rows()))/20, datum );
    matcher( ip1_copy, ip2_copy, cam1, cam2, left_tx, right_tx, forward_match,
             TerminalProgressCallback("asp","\t    Forward:") );
    matcher( ip2_copy, ip1_copy, cam2, cam1, right_tx, left_tx, backward_match,
             TerminalProgressCallback("asp","\t    Backward:") );

    // Perform circle consistency check
    size_t valid_count = 0;
    const size_t NULL_INDEX = (size_t)(-1);
    for ( size_t i = 0; i < forward_match.size(); i++ ) {
      if ( forward_match[i] != NULL_INDEX ) {
        if ( backward_match[forward_match[i]] != i ) {
          forward_match[i] = NULL_INDEX;
        } else {
          valid_count++;
        }
      }
    }
    vw_out() << "\t    Matched " << valid_count << " points." << std::endl;

    // Pull out correct subset
    std::vector<ip::InterestPoint> buffer( valid_count );
    { // Down select ip1
      size_t oi = 0;
      for ( size_t i = 0; i < forward_match.size(); i++ ) {
        if ( forward_match[i] != NULL_INDEX ) {
          buffer[oi] = ip1_copy[i];
          oi++;
        }
      }
      ip1_copy = buffer;
    }
    { // Down select ip2
      size_t oi = 0;
      for ( size_t i = 0; i < forward_match.size(); i++ ) {
        if ( forward_match[i] != NULL_INDEX ) {
          buffer[oi] = ip2_copy[forward_match[i]];
          oi++;
        }
      }
      ip2_copy = buffer;
    }

    // Apply filtering of triangulation error and altitude
    std::list<size_t> good_indices;
    if (!tri_and_alt_ip_filtering( ip1_copy, ip2_copy,
                                   cam1, cam2, datum, good_indices, left_tx, right_tx ) )
      return false;

    // Record new list that contains only the inliers.
    vw_out() << "\t    Reduced matches to " << good_indices.size() << "\n";
    buffer.clear();
    buffer.resize( good_indices.size() );

    // Subselect, Transform, Copy, Matched Ip1
    size_t w_index = 0;
    BOOST_FOREACH( size_t index, good_indices ) {
      Vector2 l( ip1_copy[index].x, ip1_copy[index].y );
      if ( transform_to_original_coord )
        l = left_tx.reverse( l );
      ip1_copy[index].ix = ip1_copy[index].x = l.x();
      ip1_copy[index].iy = ip1_copy[index].y = l.y();
      buffer[w_index] = ip1_copy[index];
      w_index++;
    }
    ip1_copy = buffer;


    // Subselect, Transform, Copy, Matched ip2
    w_index = 0;
    BOOST_FOREACH( size_t index, good_indices ) {
      Vector2 r( ip2_copy[index].x, ip2_copy[index].y );
      if ( transform_to_original_coord )
        r = right_tx.reverse( r );
      ip2_copy[index].ix = ip2_copy[index].x = r.x();
      ip2_copy[index].iy = ip2_copy[index].y = r.y();
      buffer[w_index] = ip2_copy[index];
      w_index++;
    }
    ip2_copy = buffer;

    ip::write_binary_match_file( output_name, ip1_copy, ip2_copy );

    return true;
  }

  // Calls ip matching above but with an additional step where we
  // apply a homogrpahy to make right image like left image. This is
  // useful so that both images have similar scale and similar affine qualities.
  template <class Image1T, class Image2T>
  bool ip_matching_w_alignment( vw::camera::CameraModel* cam1,
                                vw::camera::CameraModel* cam2,
                                vw::ImageViewBase<Image1T> const& image1_base,
                                vw::ImageViewBase<Image2T> const& image2_base,
                                vw::cartography::Datum const& datum,
                                std::string const& output_name,
                                double nodata1 = std::numeric_limits<double>::quiet_NaN(),
                                double nodata2 = std::numeric_limits<double>::quiet_NaN(),
                                vw::TransformRef const& left_tx = vw::TransformRef(vw::TranslateTransform(0,0)),
                                vw::TransformRef const& right_tx = vw::TransformRef(vw::TranslateTransform(0,0)) ) {
    using namespace vw;
    Image1T image1 = image1_base.impl();
    Image2T image2 = image2_base.impl();
    BBox2i box1 = bounding_box(image1), box2 = bounding_box(image2);

    // Homography is defined in the original camera coordinates
    Matrix<double> homography =
      rough_homography_fit( cam1, cam2, left_tx.reverse_bbox(box1),
                            right_tx.reverse_bbox(box2), datum );

    // Remove the main translation and solve for BBox that fits the
    // image. If we used the translation from the solved homography with
    // poorly position cameras, the right image might be moved out of
    // frame.
    homography(0,2) = homography(1,2) = 0;
    VW_OUT( DebugMessage, "asp" ) << "Aligning right to left for IP capture using rough homography: " << homography << std::endl;

    { // Check to see if this rough homography works
      HomographyTransform func( homography );
      VW_ASSERT( box1.intersects( func.forward_bbox( box2 ) ),
                 LogicErr() << "The rough homography alignment based on datum and camera geometry shows that input images do not overlap at all. Unable to proceed.\n" );
    }

    TransformRef tx( compose(right_tx, HomographyTransform(homography)) );
    BBox2i raster_box = tx.forward_bbox( right_tx.reverse_bbox(box2) );
    tx = TransformRef(compose(TranslateTransform(-raster_box.min()),
                              right_tx, HomographyTransform(homography)));
    raster_box -= Vector2i(raster_box.min());

    // It is important that we use NearestPixelInterpolation in the
    // next step. Using anything else will interpolate nodata values
    // and stop them from being masked out.
    bool inlier =
      ip_matching( cam1, cam2, image1,
                   crop(transform(image2, compose(tx, inverse(right_tx)),
                                  ValueEdgeExtension<typename Image2T::pixel_type>(boost::math::isnan(nodata2) ? 0 : nodata2),
                                  NearestPixelInterpolation()), raster_box),
                   datum, output_name, nodata1, nodata2, left_tx, tx );

    std::vector<ip::InterestPoint> ip1_copy, ip2_copy;
    ip::read_binary_match_file( output_name, ip1_copy, ip2_copy );
    Matrix<double> post_fit =
      homography_fit( ip2_copy, ip1_copy, raster_box );
    if ( sum(abs(submatrix(homography,0,0,2,2) - submatrix(post_fit,0,0,2,2))) > 4 ) {
      VW_OUT( DebugMessage, "asp" ) << "Post homography has largely different scale and skew from rough fit. Post solution is " << post_fit << "\n";
      return false;
    }

    return inlier;
  }

}

#endif//__ASP_CORE_INTEREST_POINT_MATCHING_H__
