// __BEGIN_LICENSE__
// Copyright (C) 2006-2011 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__

#ifndef __ASP_CORE_INTEGRAL_AUTO_GAIN_DETECTOR_H__
#define __ASP_CORE_INTEGRAL_AUTO_GAIN_DETECTOR_H__

#include <vw/InterestPoint/InterestData.h>
#include <vw/InterestPoint/IntegralDetector.h>
#include <vw/InterestPoint/IntegralInterestOperator.h>

namespace asp {

  class IntegralAutoGainDetector : public vw::ip::InterestDetectorBase<IntegralAutoGainDetector>,
                                   private boost::noncopyable {

  public:
    static const int IP_DEFAULT_SCALES = 8;

    IntegralAutoGainDetector( size_t max_points = 200 )
      : m_interest(0), m_scales(IP_DEFAULT_SCALES), m_obj_points(max_points) {}

    /// Detect Interest Points in the source image.
    template <class ViewT>
    vw::ip::InterestPointList process_image(vw::ImageViewBase<ViewT> const& image ) const {
      using namespace vw;
      typedef ImageView<typename PixelChannelType<typename ViewT::pixel_type>::type> ImageT;
      typedef ip::ImageInterestData<ImageT,ip::OBALoGInterestOperator> DataT;

      Timer total("\t\tTotal elapsed time", DebugMessage, "interest_point");

      // Rendering own standard copy of the image as the passed in view is just a cropview
      ImageT original_image = image.impl();

      // Producing Integral Image
      ImageT integral_image;
      {
        vw_out(DebugMessage, "interest_point") << "\tCreating Integral Image ...";
        Timer t("done, elapsed time", DebugMessage, "interest_point");
        integral_image = ip::IntegralImage( original_image );
      }

      // Creating Scales
      std::deque<DataT> interest_data;
      interest_data.push_back( DataT(original_image, integral_image) );
      interest_data.push_back( DataT(original_image, integral_image) );

      // Priming scales
      vw::ip::InterestPointList new_points;
      {
        vw_out(DebugMessage, "interest_point") << "\tScale 0 ... ";
        Timer t("done, elapsed time", DebugMessage, "interest_point");
        m_interest( interest_data[0], 0 );
      }
      {
        vw_out(DebugMessage, "interest_point") << "\tScale 1 ... ";
        Timer t("done, elapsed time", DebugMessage, "interest_point");
        m_interest( interest_data[1], 1 );
      }
      // Finally processing scales
      for ( int scale = 2; scale < m_scales; scale++ ) {

        interest_data.push_back( DataT(original_image, integral_image) );
        {
          vw_out(DebugMessage, "interest_point") << "\tScale " << scale << " ... ";
          Timer t("done, elapsed time", DebugMessage, "interest_point");
          m_interest( interest_data[2], scale );
        }

        ip::InterestPointList scale_points;

        // Detecting interest points in middle
        int32 cols = original_image.cols() - 2;
        int32 rows = original_image.rows() - 2;
        typedef typename DataT::interest_type::pixel_accessor AccessT;

        AccessT l_row = interest_data[0].interest().origin();
        AccessT m_row = interest_data[1].interest().origin();
        AccessT h_row = interest_data[2].interest().origin();
        l_row.advance(1,1); m_row.advance(1,1); h_row.advance(1,1);
        for ( int32 r=0; r < rows; r++ ) {
          AccessT l_col = l_row;
          AccessT m_col = m_row;
          AccessT h_col = h_row;
          for ( int32 c=0; c < cols; c++ ) {
            if ( is_extrema( l_col, m_col, h_col ) )
              scale_points.push_back(ip::InterestPoint(c+2,r+2,
                                                       m_interest.float_scale(scale-1),
                                                       *m_col) );
            l_col.next_col();
            m_col.next_col();
            h_col.next_col();
          }
          l_row.next_row();
          m_row.next_row();
          h_row.next_row();
        }

        // Thresholding
        threshold(scale_points, interest_data[1], scale-1);

        // Appending to the greater set
        new_points.insert(new_points.end(),
                          scale_points.begin(),
                          scale_points.end());

        // Deleting lowest
        interest_data.pop_front();
      }

      // Cull down to what we want
      if ( m_obj_points > 0 && new_points.size() > m_obj_points ) { // Cull
        VW_OUT(DebugMessage, "interest_point") << "\tCulling ...\n";
        Timer t("elapsed time", DebugMessage, "interest_point");

        int original_num_points = new_points.size();


        // Sort the interest of the points and pull out the top amount that the user wants
        new_points.sort();
        VW_OUT(DebugMessage, "interest_point") << "     Best IP : " << new_points.front().interest << std::endl;
        VW_OUT(DebugMessage, "interest_point") << "     Worst IP: " << new_points.back().interest << std::endl;
        new_points.resize( m_obj_points );

        VW_OUT(DebugMessage, "interest_point") << "     (removed " << original_num_points - new_points.size() << " interest points, " << new_points.size() << " remaining.)\n";
      } else {
        VW_OUT(DebugMessage, "interest_point") << "     Not enough IP to cull.\n";
      }

      return new_points;
    }

    // /// Detect Interest Points in the source image.
    // template <class ViewT>
    // vw::ip::InterestPointList process_image(vw::ImageViewBase<ViewT> const& image ) const {
    //   using namespace vw;
    //   typedef ImageView<typename PixelChannelType<typename ViewT::pixel_type>::type> ImageT;
    //   typedef ip::ImageInterestData<ImageT,ip::OBALoGInterestOperator> DataT;

    //   Timer total("\t\tTotal elapsed time", DebugMessage, "interest_point");

    //   // Rendering own standard copy of the image as the passed in view is just a cropview
    //   ImageT original_image = image.impl();

    //   // Producing Integral Image
    //   ImageT integral_image;
    //   {
    //     VW_OUT(DebugMessage, "interest_point") << "\tCreating Integral Image ...";
    //     Timer t("done, elapsed time", DebugMessage, "interest_point");
    //     integral_image= ip::IntegralImage( original_image );
    //   }

    //   // Creating Scales
    //   std::deque<DataT> interest_data;
    //   interest_data.push_back( DataT(original_image, integral_image) );
    //   interest_data.push_back( DataT(original_image, integral_image) );

    //   // Priming scales
    //   ip::InterestPointList new_points;
    //   {
    //     VW_OUT(DebugMessage, "interest_point") << "\tScale 0 ... ";
    //     Timer t("done, elapsed time", DebugMessage, "interest_point");
    //     m_interest( interest_data[0], 0 );
    //   }
    //   {
    //     VW_OUT(DebugMessage, "interest_point") << "\tScale 1 ... ";
    //     Timer t("done, elapsed time", DebugMessage, "interest_point");
    //     m_interest( interest_data[1], 1 );
    //   }
    //   // Finally processing scales
    //   for ( int scale = 2; scale < m_scales; scale++ ) {

    //     interest_data.push_back( DataT(original_image, integral_image) );
    //     {
    //       vw_out(DebugMessage, "interest_point") << "\tScale " << scale << " ... ";
    //       Timer t("done, elapsed time", DebugMessage, "interest_point");
    //       m_interest( interest_data[2], scale );
    //     }

    //     ip::InterestPointList scale_points;

    //     // Detecting interest points in middle
    //     int32 cols = original_image.cols() - 2;
    //     int32 rows = original_image.rows() - 2;
    //     typedef typename DataT::interest_type::pixel_accessor AccessT;

    //     AccessT l_row = interest_data[0].interest().origin();
    //     AccessT m_row = interest_data[1].interest().origin();
    //     AccessT h_row = interest_data[2].interest().origin();
    //     l_row.advance(1,1); m_row.advance(1,1); h_row.advance(1,1);
    //     for ( int32 r=0; r < rows; r++ ) {
    //       AccessT l_col = l_row;
    //       AccessT m_col = m_row;
    //       AccessT h_col = h_row;
    //       for ( int32 c=0; c < cols; c++ ) {
    //         if ( is_extrema( l_col, m_col, h_col ) )
    //           scale_points.push_back(ip::InterestPoint(c+2,r+2,
    //                                                    m_interest.float_scale(scale-1),
    //                                                    *m_col) );
    //         l_col.next_col();
    //         m_col.next_col();
    //         h_col.next_col();
    //       }
    //       l_row.next_row();
    //       m_row.next_row();
    //       h_row.next_row();
    //     }

    //     // Threshold and Harris
    //     ip::InterestPointList::iterator pos = new_points.begin();
    //     while ( pos != new_points.end() ) {
    //       if ( !m_interest.threshold( *pos, interest_data[1],
    //                                   scale-1 ) ) {
    //         pos = new_points.erase(pos);
    //       } else {
    //         pos++;
    //       }
    //     }

    //     // Appending to the greater set
    //     new_points.insert(new_points.end(),
    //                       scale_points.begin(),
    //                       scale_points.end());

    //     // Deleting lowest
    //     interest_data.pop_front();
    //   }

    //   // Cull down to what we want
    //   if ( m_obj_points > 0 && new_points.size() > m_obj_points ) { // Cull
    //     VW_OUT(DebugMessage, "interest_point") << "\tCulling ...\n";
    //     Timer t("elapsed time", DebugMessage, "interest_point");

    //     int original_num_points = new_points.size();


    //     // Sort the interest of the points and pull out the top amount that the user wants
    //     new_points.sort();
    //     VW_OUT(DebugMessage, "interest_point") << "     Best IP : " << new_points.front().interest << std::endl;
    //     VW_OUT(DebugMessage, "interest_point") << "     Worst IP: " << new_points.back().interest << std::endl;
    //     new_points.resize( m_obj_points );

    //     VW_OUT(DebugMessage, "interest_point") << "     (removed " << original_num_points - new_points.size() << " interest points, " << new_points.size() << " remaining.)\n";
    //   } else {
    //     VW_OUT(DebugMessage, "interest_point") << "     Not enough IP to cull.\n";
    //   }

    //   return new_points;
    // }

  protected:

    vw::ip::OBALoGInterestOperator m_interest;
    int m_scales;
    size_t m_obj_points;

    template <class AccessT>
    bool inline is_extrema( AccessT const& low,
                            AccessT const& mid,
                            AccessT const& hi ) const {
      AccessT low_o = low;
      AccessT mid_o = mid;
      AccessT hi_o  = hi;

      if ( *mid_o <= *low_o ) return false;
      if ( *mid_o <= *hi_o  ) return false;

      for ( vw::uint8 step = 0; step < 8; step++ ) {
        if ( step == 0 ) {
          low_o.advance(-1,-1); mid_o.advance(-1,-1);hi_o.advance(-1,-1);
        } else if ( step == 1 || step == 2 ) {
          low_o.next_col(); mid_o.next_col(); hi_o.next_col();
        } else if ( step == 3 || step == 4 ) {
          low_o.next_row(); mid_o.next_row(); hi_o.next_row();
        } else if ( step == 5 || step == 6 ) {
          low_o.prev_col(); mid_o.prev_col(); hi_o.prev_col();
        } else {
          low_o.prev_row(); mid_o.prev_row(); hi_o.prev_row();
        }
        if ( *mid <= *low_o ||
             *mid <= *mid_o ||
             *mid <= *hi_o ) return false;
      }

      return true;
    }

    template <class DataT>
    inline void threshold( vw::ip::InterestPointList& points,
                           DataT const& img_data,
                           int const& scale ) const {
      vw::ip::InterestPointList::iterator pos = points.begin();
      while (pos != points.end()) {
        if (!m_interest.threshold(*pos,
                                  img_data, scale) )
          pos = points.erase(pos);
        else
          pos++;
      }
    }
  };

}

#endif//__ASP_CORE_INTEGRAL_AUTO_GAIN_DETECTOR_H__
