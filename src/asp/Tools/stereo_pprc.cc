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


/// \file stereo_pprc.cc
///
//#define USE_GRAPHICS

#include <asp/Tools/stereo.h>
#include <asp/Core/ThreadedEdgeMask.h>
#include <asp/Core/InpaintView.h>
#include <asp/Core/AntiAliasing.h>
#include <vw/Cartography/GeoTransform.h>
#include <vw/Math/Functors.h>

using namespace vw;
using namespace asp;

namespace vw {
  template<> struct PixelFormatID<PixelMask<Vector<float, 5> > >   { static const PixelFormatEnum value = VW_PIXEL_GENERIC_6_CHANNEL; };
}

// Create the mask of pixels above threshold
struct MaskAboveThreshold: public ReturnFixedType< PixelMask<uint8> > {
  double m_threshold;
  MaskAboveThreshold(double threshold): m_threshold(threshold){}
  PixelMask<uint8> operator() (PixelGray<float> const& pix) const {
    if (pix >= m_threshold)
      return PixelMask<uint8>(255);
    else
      return PixelMask<uint8>();
  }
};
template <class ImageT>
UnaryPerPixelView<ImageT, MaskAboveThreshold>
inline mask_above_threshold( ImageViewBase<ImageT> const& image, double threshold ) {
  return UnaryPerPixelView<ImageT, MaskAboveThreshold>( image.impl(),
                                                        MaskAboveThreshold(threshold) );
}

ImageViewRef< PixelMask<uint8> > mask_and_fill_holes( ImageViewRef< PixelGray<float> > const& img,
                                                      double threshold ){
  // Create the mask of pixels above threshold. Fix any holes in it.
  ImageViewRef< PixelMask<uint8> > thresh_mask = mask_above_threshold(img, threshold);
  int max_area = 0; // fill arbitrarily big holes
  bool use_grassfire = false; // fill with default value
  PixelMask<uint8> default_inpaint_val = uint8(255);
  BlobIndexThreaded bindex( invert_mask( thresh_mask.impl() ), max_area );
  return inpaint(thresh_mask.impl(), bindex, use_grassfire, default_inpaint_val);
}

void stereo_preprocessing( Options& opt ) {

  vw_out() << "\n[ " << current_posix_time_string()
           << " ] : Stage 0 --> PREPROCESSING \n";

  std::string pre_preproc_file_left, pre_preproc_file_right;
  opt.session->pre_preprocessing_hook(opt.in_file1, opt.in_file2,
                                      pre_preproc_file_left,
                                      pre_preproc_file_right);

  DiskImageView<PixelGray<float> > left_image(pre_preproc_file_left),
    right_image(pre_preproc_file_right);

  bool rebuild = false;
  try {
    vw_log().console_log().rule_set().add_rule(-1,"fileio");
    DiskImageView<PixelGray<uint8> > testa(opt.out_prefix+"-lMask.tif");
    DiskImageView<PixelGray<uint8> > testb(opt.out_prefix+"-rMask.tif");
    vw_settings().reload_config();
  } catch (vw::IOErr const& e) {
    vw_settings().reload_config();
    rebuild = true;
  } catch (vw::ArgumentErr const& e ) {
    // Throws on a corrupted file.
    vw_settings().reload_config();
    rebuild = true;
  }
  if (rebuild) {
    vw_out() << "\t--> Generating image masks... \n";

    cartography::GeoReference left_georef, right_georef;
    ImageViewRef< PixelMask<uint8> > left_mask = copy_mask(constant_view(uint8(255),
                                                                         left_image.cols(), left_image.rows()),
                                                           asp::threaded_edge_mask(left_image,0,0,1024)
                                                           );
    ImageViewRef< PixelMask<uint8> > right_mask = copy_mask(constant_view(uint8(255),
                                                                          right_image.cols(), right_image.rows() ),
                                                            asp::threaded_edge_mask(right_image,0,0,1024));

    double left_threshold  = stereo_settings().nodata_threshold;
    double right_threshold = stereo_settings().nodata_threshold;
    double nodata_fraction = stereo_settings().nodata_percentage/100.0;
    double nodata_factor   = stereo_settings().nodata_optimal_threshold_factor;
    if (int(!std::isnan(left_threshold))  +
        int(!std::isnan(nodata_fraction)) +
        int(!std::isnan(nodata_factor)) >= 2
        ){
      vw_throw( ArgumentErr()
                << "\nAt most one of the no-data settings "
                << "(threshold, percentage, or optimal threshold factor) must be set.\n");
    }

    if ( !std::isnan(nodata_factor) ){
      // Find the black pixels threshold using Otsu's optimal threshold method.
      left_threshold  = nodata_factor*optimal_threshold(left_image);
      right_threshold = nodata_factor*optimal_threshold(right_image);
    }

    if ( !std::isnan(nodata_fraction) ){
      // Declare a fixed proportion of pixels to be black.

      math::CDFAccumulator< PixelGray<float> > left_cdf(1024, 1024), right_cdf(1024, 1024);
      for_each_pixel( left_image, left_cdf );
      for_each_pixel( right_image, right_cdf );

      left_threshold  = left_cdf.quantile(nodata_fraction);
      right_threshold = right_cdf.quantile(nodata_fraction);
    }

    if ( !std::isnan(left_threshold) && !std::isnan(right_threshold) ){
      // Mask pixels below threshold.

      ImageViewRef< PixelMask<uint8> > left_thresh_mask = mask_and_fill_holes(left_image, left_threshold);
      left_mask = intersect_mask(left_mask, left_thresh_mask);

      ImageViewRef< PixelMask<uint8> > right_thresh_mask = mask_and_fill_holes(right_image, right_threshold);
      right_mask = intersect_mask(right_mask, right_thresh_mask);
    }

    bool has_left_georef  = read_georeference(left_georef,  opt.in_file1);
    bool has_right_georef = read_georeference(right_georef, opt.in_file2);
    if (has_left_georef && has_right_georef){

      // Intersect the left mask with the warped version of the right mask, and vice-versa
      // to reduce noise.

      ImageViewRef< PixelMask<uint8> > warped_left_mask = crop(vw::cartography::geo_transform
                                                               (left_mask,
                                                                left_georef,
                                                                right_georef,
                                                                ConstantEdgeExtension(),
                                                                NearestPixelInterpolation()),
                                                               bounding_box(right_mask)
                                                               );
      ImageViewRef< PixelMask<uint8> > warped_right_mask = crop(vw::cartography::geo_transform
                                                                (right_mask,
                                                                 right_georef,
                                                                 left_georef,
                                                                 ConstantEdgeExtension(),
                                                                 NearestPixelInterpolation()),
                                                                bounding_box(left_mask)
                                                                );

      asp::block_write_gdal_image( opt.out_prefix+"-lMask.tif",
                                   apply_mask(intersect_mask(left_mask, warped_right_mask)),
                                   opt, TerminalProgressCallback("asp", "\t    Mask L: ") );
      asp::block_write_gdal_image( opt.out_prefix+"-rMask.tif",
                                   apply_mask(intersect_mask(right_mask, warped_left_mask)),
                                   opt, TerminalProgressCallback("asp", "\t    Mask R: ") );

    }else{

      asp::block_write_gdal_image( opt.out_prefix+"-lMask.tif",
                                   apply_mask(left_mask),
                                   opt, TerminalProgressCallback("asp", "\t    Mask L: ") );
      asp::block_write_gdal_image( opt.out_prefix+"-rMask.tif",
                                   apply_mask(right_mask),
                                   opt, TerminalProgressCallback("asp", "\t    Mask R: ") );

    }

  }

  try {
    // This confusing try catch is to see if the subsampled images
    // actually have content.
    DiskImageView<PixelGray<float> > testa(opt.out_prefix+"-L_sub.tif");
    DiskImageView<PixelGray<float> > testb(opt.out_prefix+"-R_sub.tif");
    vw_out() << "\t--> Using cached subsampled image.\n";
  } catch (vw::Exception const& e) {
    // Produce subsampled images, these will be used later for Auto
    // search range. They're also a handy debug tool.
    float sub_scale =
      sqrt(1500.0 * 1500.0 / (float(left_image.cols()) * float(left_image.rows())));
    sub_scale +=
      sqrt(1500.0 * 1500.0 / (float(right_image.cols()) * float(right_image.rows())));
    sub_scale /= 2;
    if ( sub_scale > 0.6 ) sub_scale = 0.6;

    // Solving for the number of threads and the tile size to use for
    // subsampling while only using 500 MiB of memory. (The cache code
    // is a little slow on releasing so it will probably use 1.5GiB
    // memory during subsampling) Also tile size must be a power of 2
    // and greater than or equal to 64 px;
    uint32 sub_threads = vw_settings().default_num_threads() + 1;
    uint32 tile_power = 0;
    while ( tile_power < 6 && sub_threads > 1) {
      sub_threads--;
      tile_power = boost::numeric_cast<uint32>( log10(500e6*sub_scale*sub_scale/(4.0*float(sub_threads)))/(2*log10(2)));
    }
    uint32 sub_tile_size = 1u << tile_power;
    if ( sub_tile_size > vw_settings().default_tile_size() )
      sub_tile_size = vw_settings().default_tile_size();

    // Change writing parameters to ideal threads and tiles
    vw_out() << "\t--> Creating previews. Subsampling by " << sub_scale
             << " by using " << sub_tile_size << " tile size and "
             << sub_threads << " threads.\n";
    Vector2i previous_tile_size = opt.raster_tile_size;
    opt.raster_tile_size = Vector2i(sub_tile_size,sub_tile_size);
    uint32 previous_num_threads = vw_settings().default_num_threads();
    vw_settings().set_default_num_threads(sub_threads);

    if ( sub_scale > 0.5 ) {
      // When we are near the pixel input to output ratio, standard
      // interpolation is our best possible results.
      asp::block_write_gdal_image( opt.out_prefix+"-L_sub.tif",
                                   resample(left_image,sub_scale), opt,
                                   TerminalProgressCallback("asp", "\t    Sub L: ") );
      asp::block_write_gdal_image( opt.out_prefix+"-R_sub.tif",
                                   resample(right_image,sub_scale), opt,
                                   TerminalProgressCallback("asp", "\t    Sub R: ") );
      asp::block_write_gdal_image( opt.out_prefix+"-lMask_sub.tif",
                                   resample(DiskImageView<uint8>(opt.out_prefix+"-lMask.tif"),sub_scale,
                                            ZeroEdgeExtension(),
                                            NearestPixelInterpolation()), opt,
                                   TerminalProgressCallback("asp", "\t    Sub L Mask: ") );
      asp::block_write_gdal_image( opt.out_prefix+"-rMask_sub.tif",
                                   resample(DiskImageView<uint8>(opt.out_prefix+"-rMask.tif"),sub_scale,
                                            ZeroEdgeExtension(),
                                            NearestPixelInterpolation()), opt,
                                   TerminalProgressCallback("asp", "\t    Sub R Mask: ") );
    } else {
      // When we heavily reduce the image size, super sampling seems
      // like the best approach. The method below should be equivalent
      DiskImageView<uint8> lmask(opt.out_prefix+"-lMask.tif"),
        rmask(opt.out_prefix+"-rMask.tif");
      asp::block_write_gdal_image( opt.out_prefix+"-L_sub.tif",
                                   cache_tile_aware_render(apply_mask(resample_aa( copy_mask(left_image,create_mask(lmask) ), sub_scale )), Vector2i(256,256) * sub_scale),
                                   opt, TerminalProgressCallback("asp", "\t    Sub L: ") );
      asp::block_write_gdal_image( opt.out_prefix+"-R_sub.tif",
                                   cache_tile_aware_render(apply_mask(resample_aa( copy_mask(right_image,create_mask(rmask) ), sub_scale )), Vector2i(256,256) * sub_scale),
                                   opt, TerminalProgressCallback("asp", "\t    Sub R: ") );
      asp::block_write_gdal_image( opt.out_prefix+"-lMask_sub.tif",
                                   resample(lmask,sub_scale,ZeroEdgeExtension(), NearestPixelInterpolation()), opt,
                                   TerminalProgressCallback("asp", "\t    Sub L Mask: ") );
      asp::block_write_gdal_image( opt.out_prefix+"-rMask_sub.tif",
                                   resample(rmask,sub_scale,ZeroEdgeExtension(), NearestPixelInterpolation()), opt,
                                   TerminalProgressCallback("asp", "\t    Sub R Mask: ") );
    }
    opt.raster_tile_size = previous_tile_size;
    vw_settings().set_default_num_threads(previous_num_threads);
  }
}

int main(int argc, char* argv[]) {

  stereo_register_sessions();
  Options opt;
  try {
    handle_arguments( argc, argv, opt,
                      PreProcessingDescription() );

    // Internal Processes
    //---------------------------------------------------------
    vw_out() << "Using \"" << opt.stereo_default_filename << "\"\n";
    stereo_preprocessing( opt );

    vw_out() << "\n[ " << current_posix_time_string()
             << " ] : PREPROCESSING FINISHED \n";

  } ASP_STANDARD_CATCHES;

  return 0;
}
