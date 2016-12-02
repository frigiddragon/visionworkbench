// __BEGIN_LICENSE__
//  Copyright (c) 2006-2013, United States Government as represented by the
//  Administrator of the National Aeronautics and Space Administration. All
//  rights reserved.
//
//  The NASA Vision Workbench is licensed under the Apache License,
//  Version 2.0 (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// __END_LICENSE__


#ifndef __VW_RADAR_H__
#define __VW_RADAR_H__

#include <stdlib.h>
#include <vw/Core/Functors.h>
#include <vw/Math/Functors.h>
#include <vw/Math/Statistics.h>
#include <vw/Image/Algorithms.h>
#include <vw/Image/Filter.h>
#include <vw/Image/BlobIndex.h>
#include <vw/Image/Statistics.h>
#include <vw/Image/PixelMask.h>
#include <vw/Image/Manipulation.h>
#include <vw/Image/Transform.h>
#include <vw/Image/ImageIO.h>
#include <vw/Image/PixelMask.h>
#include <vw/Image/MaskViews.h>
#include <vw/Image/UtilityViews.h>
#include <vw/Image/PerPixelViews.h>
#include <vw/FileIO/DiskImageView.h>
#include <vw/Cartography/GeoReferenceUtils.h>
#include <vw/Cartography/GeoTransform.h>

/**
  Tools for processing radar data.

*/

namespace vw {
namespace radar {


//=========================================================================================

// TODO: Move these

/// Standard Z shaped fuzzy math membership function between a and b
template <typename T>
class FuzzyMembershipZFunctor : public ReturnFixedType<T> {
  float m_a, m_b, m_c, m_dba; ///< Constants
public:
  /// Constructor
  FuzzyMembershipZFunctor(float a, float b) : m_a(a), m_b(b), m_c((a+b)/2.0), m_dba(b-a) {}

  /// Apply Z function
  T operator()(T const& v) const {
    if (v < m_a)
      return T(1.0);
    if (v < m_c)
      return T(1.0 - 2.0*pow(((v-m_a)/m_dba), 2.0));
    if (v < m_b)
      return T(2.0*pow(((v-m_b)/m_dba), 2.0));
    return T(0.0);
  }
}; // End class FuzzyMembershipZFunctor


/// Standard S shaped fuzzy math membership function between a and b
template <typename T>
class FuzzyMembershipSFunctor : public ReturnFixedType<T> {
  float m_a, m_b, m_c, m_dba; ///< Constants
public:
  /// Constructor
  FuzzyMembershipSFunctor(float a, float b) : m_a(a), m_b(b), m_c((a+b)/2.0), m_dba(b-a) {}

  /// Apply S function
  T operator()(T const& v) const {
    if (v < m_a)
        return T(0.0);
    if (v < m_c)
        return T(2*pow(((v-m_a)/m_dba), 2.0));
    if (v < m_b)
        return T(1.0 - 2*pow(((v-m_b)/m_dba), 2.0));
    return T(1.0);
  }
}; // End class FuzzyMembershipSFunctor



//=========================================================================================

// TODO: Consolidate these functions in vw/math/statistics
// TODO: Really need a histogram class!

/// As part of the Kittler/Illingworth method, compute J(T) for a given T
/// - Input should be a percentage histogram
double compute_kittler_illingworth_jt(std::vector<double> const& histogram,
                   int num_bins, double min_val, double max_val, int bin) {

    double FAIL_VAL = 999999.0; // Just returning a high error should work fine

    // For convenience, generate a list of bin values.
    std::vector<double> bin_values(num_bins);
    double bin_width = (max_val - min_val) / num_bins;
    for (int i=0; i<num_bins; ++i)
      bin_values[i] = min_val + i*bin_width;

    // Compute the total value in the two classes and the mean value.
    double P1 = 0, P2 = 0;
    double weighted_sum1=0, weighted_sum2=0;
    for (int i=0; i<=bin; ++i) {
      P1 += histogram[i];
      weighted_sum1 += histogram[i]*bin_values[i];
    }
    for (int i=bin+1; i<num_bins; ++i) {
      P2 += histogram[i];
      weighted_sum2 += histogram[i]*bin_values[i];
    }

    // Only continue if both classes contain at least one pixel.
    if ((P1 <= 0) or (P2 <= 0))
        return FAIL_VAL;
    double mean1 = weighted_sum1 / P1;
    double mean2 = weighted_sum2 / P2;

    // Compute the standard deviations of the classes.
    double sigma1=0, sigma2=0;
    for (int i=0; i<=bin; ++i)
      sigma1 += pow(bin_values[i] - mean1, 2.0) * histogram[i];
    for (int i=bin+1; i<num_bins; ++i)
      sigma2 += pow(bin_values[i] - mean2, 2.0) * histogram[i];
    sigma1 /= P1;
    sigma2 /= P2;

    // Make sure both classes contain at least two intensity values.
    if ((sigma1 <= 0) or (sigma2 <= 0))
        return FAIL_VAL;

    // Compute J(T).
    double J = 1.0 + 2.0*(P1*log(sigma1) + P2*log(sigma2)) 
                   - 2.0*(P1*log(P1    ) + P2*log(P2    ));
    return J;
} // End function compute_kittler_illingworth_jt

/// Tries to compute an optimal histogram threshold using the Kittler/Illingworth method
double split_histogram_kittler_illingworth(std::vector<double> const& histogram,
                                     int num_bins, double min_val, double max_val) {
  double bin_width = (max_val - min_val) / num_bins;

  /*// DEBUG: Write out the histogram
  std::cout << std::endl;
  for (int i=0; i<num_bins; ++i) {
    std::cout << min_val + bin_width*i<< " ";
  }
  std::cout << std::endl;
  for (int i=0; i<num_bins; ++i) {
    std::cout << histogram[i] << " ";
  }
  std::cout << std::endl;
  */

  // Normalize the histogram (each bin is now a percentage)
  std::vector<double> histogram_percentages(num_bins);
  double sum = 0;
  for (int i=0; i<num_bins; ++i) // Compute sum
    sum += histogram[i];
  for (int i=0; i<num_bins; ++i) // Divide
    histogram_percentages[i] = histogram[i] / sum;


  // Try out every bin value in the histogram and pick the best one
  //  - Skip the first bin due to computation below.
  //  - For more resolution, use more bins!
  //  - TODO: write up a smarter solver for this.
  
  // Compute score for each possible answer.
  std::vector<double> scores(num_bins-1);
  for (int i=0; i<num_bins-1; ++i)
    scores[i] = compute_kittler_illingworth_jt(histogram_percentages, num_bins, min_val, max_val, i+1);

  // Find the lowest score      
  double min_score = scores[0];
  int    min_index = 0;
  for (int i=0; i<num_bins-1; ++i) {
    if (scores[i] < min_score) {
      min_score = scores[i];
      min_index = i+1;
    }
  }
  // Compute the final threshold which is below the current bin value
  double threshold = min_val + bin_width*(static_cast<double>(min_index) - 0.5);
  
  return threshold;
} // End function split_histogram_kittler_illingworth


// TODO: Move this

/// Splits up one large BBox into a grid of smaller BBoxes.
/// - Returns the number of BBoxes created.
/// - If include_partials is set to false, incomplete BBoxes will be discarded.
int divide_roi(BBox2i const& full_roi, int size,
               std::vector<std::vector<BBox2i> > &new_rois, bool include_partials = true) {
               
  // Compute the number of boxes
  double dsize = static_cast<double>(size);
  int num_boxes_x, num_boxes_y;
  if (include_partials) {
    num_boxes_x = ceil(static_cast<double>(full_roi.width())  / dsize);
    num_boxes_y = ceil(static_cast<double>(full_roi.height()) / dsize);
  } else { // Discard partial boxes
    num_boxes_x = floor(static_cast<double>(full_roi.width())  / dsize);
    num_boxes_y = floor(static_cast<double>(full_roi.height()) / dsize);
  }

  std::cout << "Dividing up ROI " << full_roi << std::endl;
  printf("Num boxes: %d, %d\n", num_boxes_x, num_boxes_y);

  // Generate all of the boxes
  int max_x = full_roi.max()[0];
  int max_y = full_roi.max()[1];
  int num_boxes = num_boxes_y*num_boxes_x;
  new_rois.resize(num_boxes_y);
  for (int r=0; r<num_boxes_y; ++r) {
    new_rois[r].resize(num_boxes_x);
    for (int c=0; c<num_boxes_x; ++c) {
      // Set up each box being mindful of partial boxes at the edges
      int x      = c*size;
      int y      = r*size;
      int height = size;
      int width  = size;
      if (y+height > max_y) height = max_y - y;
      if (x+width  > max_x) width  = max_x - x;
      new_rois[r][c] = BBox2i(x, y, width, height);
      //std::cout << "--> New ROI: " << new_rois[r][c] << std::endl;
    }
  }
  return num_boxes;
}





//=========================================================================================

// TODO: What data type to use?
typedef uint16 Sentinel1Type;
typedef float  RadarType;
typedef PixelMask<RadarType> RadarTypeM;


/// Convert a sentinel1 image from digital numbers (DN) to decibels (DB)
struct Sentinel1DnToDb : public ReturnFixedType<float> {
  Sentinel1DnToDb(){}
  float operator()( RadarType value ) const {
    if (value == 0)
      return 0; // These pixels are invalid, don't return inf for them!
    return 10*log10(value);
  }
};

/// Convert a sentinel1 image from digital numbers (DN) to decibels (DB)
template <class ImageT>
UnaryPerPixelView<ImageT,Sentinel1DnToDb>
inline sentinel1_dn_to_db( ImageViewBase<ImageT> const& image) {
  return UnaryPerPixelView<ImageT,Sentinel1DnToDb>( image.impl(), Sentinel1DnToDb() );
}


/// Crop and preprocess the input image in preparation for the sar_martinis algorithm.
void preprocess_sentinel1_image(ImageView<Sentinel1Type> const& input_image, BBox2i const& roi,
                                RadarType &global_min, RadarType &global_max,
                                cartography::GdalWriteOptions const& write_options,
                                std::string const& temporary_path,
                                ImageViewRef<RadarTypeM>      & processed_image) {

  // Currently we write the preprocessed image to disk, but maybe in the future
  // we should not.

  // TODO: Apply processing!

  // --> For now, leave the image as-is knowing that most pixels fall in the 0-1000 range.
  //  ---> Later, apply a log scale or something.

  // TODO: Need these four values to rescale later!

  global_min = 0.0;  // This can be kept constant
  global_max = 35.0; // TODO: Is it worth computing the exact value?

  const double PROC_MIN = 0;
  const double PROC_MAX = 400;
/*
  // TODO: Record this gain/offset so we can undo this scaling later on!
  double input_range  = global_max - global_min;
  double output_range = PROC_MAX - PROC_MIN;
  double gain         = output_range / input_range;
  double offset       = PROC_MIN - global_min*gain;
  printf("Computed gain = %lf, offset = %lf\n", gain, offset);
*/

  //std::cout << "minmax...\n";
  //min_max_channel_values(DiskImageView<RadarType>("preprocessed_image.tif"), global_min, global_max);
  //printf("Computed min = %f, max = %f\n", global_min, global_max);

  //TODO: Handle the mask in the filter!
  // Perform median filter to correct speckles (see section 2.1.4)
  int kernel_size = 3;
  cartography::block_write_gdal_image(temporary_path,
                                      normalize(sentinel1_dn_to_db(median_filter_view(crop(input_image, roi), 
                                                                                      Vector2i(kernel_size, kernel_size))
                                                                ),
                                              global_min, global_max, PROC_MIN, PROC_MAX
                                             ),
                                      write_options,
                                      TerminalProgressCallback("vw", "\t--> Preprocessing:"));

  //TODO: How to handle input NODATA!
  // Return a view of the image on disk for easy access
  processed_image = create_mask(DiskImageView<RadarType>("preprocessed_image.tif"), 0);

  // Update these to reflect the scaled values
  global_min = PROC_MIN;
  global_max = PROC_MAX; 

 

} // End preprocess_sentinel1_image



// TODO: Replace with a simpler multi-threaded processing method to get the means!
template <class ImageT>
class ImageTileMeansView : public ImageViewBase<ImageTileMeansView<ImageT> > {

public: // Definitions

  // This only controls junk written to disk so use a small type.
  typedef uint8      pixel_type;
  typedef pixel_type result_type;

private: // Variables

  ImageT const& m_input_image;
  mutable ImageView<RadarTypeM> m_tile_means, m_tile_stddevs;
  int m_tile_size;

public: // Functions

  // Constructor
  ImageTileMeansView( ImageT  const& input_image, int num_boxes_x, int num_boxes_y, int tile_size)
                  : m_input_image(input_image), m_tile_size(tile_size){
    // Init the true output objects
    m_tile_means.set_size(num_boxes_x,   num_boxes_y);
    m_tile_stddevs.set_size(num_boxes_x, num_boxes_y);
  }

  inline int32 cols  () const { return m_input_image.cols(); }
  inline int32 rows  () const { return m_input_image.rows(); }
  inline int32 planes() const { return 1; }

  inline result_type operator()( int32 i, int32 j, int32 p=0 ) const
  { return 0; } // NOT IMPLEMENTED!

  // Accessors to grab the results
  ImageView<RadarTypeM> const& tile_means  () const {return m_tile_means;}
  ImageView<RadarTypeM> const& tile_stddevs() const {return m_tile_stddevs;}
 
  typedef ProceduralPixelAccessor<ImageTileMeansView<ImageT> > pixel_accessor;
  inline pixel_accessor origin() const { return pixel_accessor( *this, 0, 0 ); }

  /// Compute the mean and percentage of valid pixels in an image
  double mean_and_validity(CropView<ImageView<RadarTypeM> > const& image, double &mean) const {
    mean = 0;
    double count = 0, sum = 0;
    for (int r=0; r<image.rows(); ++r) {
      for (int c=0; c<image.cols(); ++c) {
        if (is_valid(image(c,r))) {
          sum   += image(c,r);
          count += 1.0;
        }
      }
    }
    if (count > 0)
      mean = sum / count;
    return count / static_cast<double>(image.rows() * image.cols());
  }
                           

  typedef CropView<ImageView<result_type> > prerasterize_type;
  inline prerasterize_type prerasterize( BBox2i const& bbox ) const {

    // Figure out which tile this is
    int this_col = bbox.min()[0] / m_tile_size;
    int this_row = bbox.min()[1] / m_tile_size;

    // Skip processing for this tile if it falls out of bounds (can happen on borders)
    if ( (this_col < m_tile_means.cols()) && (this_row < m_tile_means.rows())) {
      // Compute the four sub-ROIs
      const int NUM_SUB_ROIS = 4;
      int hw = bbox.width() /2;
      int hh = bbox.height()/2;
      std::vector<BBox2i> sub_rois(NUM_SUB_ROIS); // ROIs relative to the whole tile
      std::vector<double> means;
      means.reserve(NUM_SUB_ROIS);
      sub_rois[0] = BBox2i(0,  0,  hw, hh); // Top left
      sub_rois[1] = BBox2i(hw, 0,  hw, hh); // Top right
      sub_rois[2] = BBox2i(hw, hh, hw, hh); // Bottom right
      sub_rois[3] = BBox2i(0,  hh, hw, hh); // Bottom left
      
      //printf("Means for tile: %d, %d\n", this_col, this_row);
      
      ImageView<RadarTypeM> section = crop(m_input_image, bbox);

      // Don't compute statistics from regions with a lot of bad pixels
      const double MIN_PERCENT_VALID = 0.9;
      
      // Compute the mean in each of the four sub-rois
      double percent_valid;
      for (int i=0; i<NUM_SUB_ROIS; ++i) {
        double mean;
        percent_valid = mean_and_validity(crop(section, sub_rois[i]), mean);
        if (percent_valid >= MIN_PERCENT_VALID)
          means.push_back(mean);
        //std::cout << "Mean for sub-roi " << sub_rois[i] << " = " << means[i] << std::endl;
      }
      bool is_valid = false;
      double mean_of_means = 0;
      double stddev_of_means = 0;
      if (means.size() > 0) {
        // Compute the standard deviation of the means
        // - Currently both are set to zero if all pixels are invalid.
        mean_of_means = math::mean(means);
        is_valid = (mean_of_means > 0);
        if (is_valid) {
          stddev_of_means = math::standard_deviation(means, mean_of_means);
          //printf("PV, mean, stddev for tile (%d, %d) = %lf, %lf, %lf\n", 
          //      this_col, this_row, percent_valid, mean_of_means, stddev_of_means);
        }
}

      // Assign the REAL outputs
      if (is_valid) {
        m_tile_means  (this_col, this_row) = RadarTypeM(mean_of_means);
        m_tile_stddevs(this_col, this_row) = RadarTypeM(stddev_of_means);
      } else {
        invalidate(m_tile_means  (this_col, this_row));
        invalidate(m_tile_stddevs(this_col, this_row));
      }
    } // End mean/stddev calculation

    // Set up the output image tile - This is junk we don't care about!
    ImageView<result_type> tile(bbox.width(), bbox.height());

    // Return the tile we created with fake borders to make it look the size of the entire output image
    return prerasterize_type(tile,
                             -bbox.min().x(), -bbox.min().y(),
                             cols(), rows() );

  } // End prerasterize function

 template <class DestT>
 inline void rasterize( DestT const& dest, BBox2i const& bbox ) const {
   vw::rasterize( prerasterize(bbox), dest, bbox );
 }

}; // End class ImageTileMeansView



template <class ImageT>
void generate_tile_means(ImageT input_image, int tile_size, int num_boxes_x, int num_boxes_y,
                         ImageView<RadarTypeM> &tile_means, ImageView<RadarTypeM> &tile_stddevs) {

  // These tiles must be written at this exact size to get the correct results!
  //Vector2i block_size(tile_size, tile_size);
  
  ImageTileMeansView<ImageT> tile_mean_generator(input_image, num_boxes_x, num_boxes_y, tile_size);
  
  std::string dummy_path = "dummy.tif";
  
  // TODO: Using this View object is a hack to get multi-threaded processing
  //       and it should be replaced with a simpler implementation!!! 
  
  // Write dummy image to force multi-threaded processing
  cartography::GdalWriteOptions write_options;
  write_options.raster_tile_size = Vector2i(tile_size, tile_size);
  block_write_gdal_image(dummy_path, tile_mean_generator, write_options,
                         TerminalProgressCallback("vw", "\t--> Computing tile means:"));

  // Grab results from the view object
  tile_means   = tile_mean_generator.tile_means();
  tile_stddevs = tile_mean_generator.tile_stddevs();
  
  // Clean up the dummy file we wrote
  std::remove(dummy_path.c_str());

} // End function generate_tile_means




/// Combine the four fuzzy scores into one final score
/// - Inputs are expected to be in the range 0-1
template <class PixelT1, class PixelT2, class PixelT3, class PixelT4>
struct DefuzzFunctor : public ReturnFixedType<float> {

  typedef PixelMask<float> result_type;

  result_type operator()(PixelT1 const& p1, PixelT2 const& p2, PixelT3 const& p3, PixelT4 const& p4) const {
    // If any input score is zero, the output score is zero.
    if ((p1 == 0) || (p2 == 0) || (p3 == 0) || (p4 == 0))
      return result_type(0.0);

    float mean = (p1 + p2 + p3 + p4) / 4.0;
    return result_type(mean);
  }
}; // End class DefuzzFunctor


//============================================================================

// TODO: Move these


// Define comparison function (just compare the first elements)
template <typename T>
bool less_than_function_pair_first( const T& a, const T& b) { 
  return a.first < b.first; 
}


/// Generate a sorted list of indices into an input vector.
/// - Intended for simple vectors of ints or floats.
template <typename T>
void sort_vector_indices(std::vector<T> const& v, std::vector<size_t> &indices) {

  // Copy data into a new vector with indices
  typedef std::pair<T, size_t> PairV;
  std::vector<PairV> v_new(v.size());
  for (size_t i=0; i<v.size(); ++i) {
    v_new[i].first  = v[i];
    v_new[i].second = i;
  }

  // Run sort and store the sorted indices
  std::sort(v_new.begin(), v_new.end(), less_than_function_pair_first<PairV>);
  indices.resize(v.size());
  for (size_t i=0; i<v.size(); ++i)
    indices[i] = v_new[i].second;
}


/// Converts a normal vector into a slope angle in degrees.
struct GetAngleFunc : public ReturnFixedType<PixelMask<float> > {
  PixelMask<float> operator() (PixelMask<Vector3f> const& pix) const {
    if (is_transparent(pix))
      return PixelMask<float>();
    const float RAD2DEG = 180.0 / 3.14159; // TODO: Where do we keep our constants?
    return PixelMask<float>(RAD2DEG*acos(fabs(dot_prod(pix.child(),Vector3(0,0,1)))));
  }
};
template <class ViewT>
UnaryPerPixelView<ViewT, GetAngleFunc> get_angle(ImageViewBase<ViewT> const& view) {
  return UnaryPerPixelView<ViewT, GetAngleFunc>(view.impl(), GetAngleFunc());
}



/// Select the best N tiles to use for computing the global water threshold
size_t select_best_tiles(ImageView<RadarTypeM> & tile_means, ImageView<RadarTypeM> & tile_stddevs,
                         std::vector<Vector2i> & kept_tile_indices,
                         cartography::GdalWriteOptions const& write_options) {

  std::cout << "Computing global tile statistics...\n";

  // Compute the global mean
  double global_mean = mean_channel_value(tile_means);

  std::cout << "Computing global mean = " << global_mean << "\n";

  // Compute 95% quantile standard deviation
  double stddev_min, stddev_max;
  find_image_min_max(tile_stddevs, stddev_min, stddev_max);
  
  int    num_bins = 255;
  std::vector<double> hist;
  histogram(tile_stddevs, num_bins, stddev_min, stddev_max, hist);
  const double TILE_STDDEV_PERCENTILE_CUTOFF = 0.95;
  int    bin = get_histogram_percentile(hist, TILE_STDDEV_PERCENTILE_CUTOFF);
  double bin_width      = (stddev_max - stddev_min)/static_cast<double>(num_bins);
  double std_dev_cutoff = stddev_min + bin_width*bin;
  std::cout << "std_dev_cutoff " << std_dev_cutoff << "\n";

  // Select the tiles with the highest STD values (N')
  ImageView<uint8> kept_tile_display;
  kept_tile_display.set_size(tile_means.cols(), tile_means.rows());
  std::vector<Vector2i> n_prime_tiles;
  std::vector<double  > n_prime_std_dev, n_prime_mean;
  double mean_of_selected = 0;
  for (int r=0; r<tile_stddevs.rows(); ++r) {
    for (int c=0; c<tile_stddevs.cols(); ++c) {
      // The tile must have a high stddev and also be below the global mean
      //  since water tends to be darker than land.
      if ( (tile_stddevs(c,r) > std_dev_cutoff) &&
           (tile_means  (c,r) < global_mean   )   ) {
        n_prime_tiles.push_back(Vector2i(c,r));
        n_prime_std_dev.push_back(tile_stddevs(c,r));
        n_prime_mean.push_back   (tile_means  (c,r));
        mean_of_selected += tile_means(c,r);
        std::cout << "Keeping tile " << Vector2i(c,r) << std::endl;
        kept_tile_display(c,r) = 255;
      }
    }
  }

  size_t num_tiles_kept = n_prime_tiles.size();
  std::cout << "Selected " << num_tiles_kept << " initial tiles.\n";
  
  block_write_gdal_image("initial_kept_tiles.tif", kept_tile_display, write_options); // DEBUG

  if (n_prime_tiles.empty())
    vw_throw(LogicErr() << "No tiles left after std_dev filtering!");

  // Cap the number of selected tiles
  const size_t MAX_NUM_TILES = 5; // From the paper  
  
  // If already at/below the cap we are finished.
  if (num_tiles_kept <= MAX_NUM_TILES) {
    kept_tile_indices = n_prime_tiles;
    return n_prime_tiles.size();
  }
  // Reset this image DEBUG
  fill(kept_tile_display, 0);
  
  // The original paper restricted kept tiles to tiles below the tile mean,
  // so go back and add this if it seems to be needed.
  
  // Keep the N tiles with the highest standard deviation
  std::vector<size_t> indices;
  sort_vector_indices(n_prime_std_dev, indices); // Sorts by STD low to high
  
  kept_tile_indices.resize(MAX_NUM_TILES);
  size_t max_index = num_tiles_kept - 1;
  size_t index_out=0;
  for (size_t i=max_index; i>max_index-MAX_NUM_TILES; --i) {
    kept_tile_indices[index_out] = n_prime_tiles[i];
    kept_tile_display(n_prime_tiles[i][0],n_prime_tiles[i][1]) = 255;
    std::cout << "Keeping tile " << n_prime_tiles[i] << std::endl;
    ++index_out;
  }

  block_write_gdal_image("final_kept_tiles.tif", kept_tile_display, write_options); // DEBUG
  
  std::cout << "Reduced to " << MAX_NUM_TILES << " kept tiles.\n";
  
  return MAX_NUM_TILES;
} // End function select_best_tiles



bool compute_global_threshold(ImageViewRef<RadarTypeM> const& preprocessed_image, 
                              std::vector<Vector2i> const& kept_tile_indices,
                              std::vector<std::vector<BBox2i> > const& large_tile_boxes,
                              float global_min, float global_max,
                              double &threshold_mean) {

  // For each selected tile, find optimal threshold using Kittler-Illingworth method.
  // - TODO: Does any rescaling need to be applied to these thresholds values?
  //         splitValDb = rescaleNumber(splitVal, PROC_global_min, PROC_global_max, minVal, maxVal)        
  // - TODO: Compute this in parallel!
  int num_bins = 255;
  const size_t num_tiles = kept_tile_indices.size();
  std::vector<double> optimal_tile_thresholds(num_tiles);
  double dmin = static_cast<double>(global_min);
  double dmax = static_cast<double>(global_max);
  threshold_mean = 0;
  for (size_t i=0; i<num_tiles; ++i) {
  
    // Compute tile histogram     
    Vector2i tile_index = kept_tile_indices[i];
    BBox2i   roi = large_tile_boxes[tile_index[1]][tile_index[0]]; // The ROIs are stored row-first.
    std::vector<double> hist;
    histogram(crop(preprocessed_image, roi), num_bins, dmin, dmax, hist);
    
    // Compute optimal split
    optimal_tile_thresholds[i] = split_histogram_kittler_illingworth(hist, num_bins, global_min, global_max);
    std::cout << "For ROI " << roi << ", computed threshold " << optimal_tile_thresholds[i] << std::endl;
    threshold_mean += optimal_tile_thresholds[i];
  }
  threshold_mean /= static_cast<double>(num_tiles);

  // TODO: Check statistics of the computed tile thresholds
  // TODO: Some method to discard outliers

  // If the standard deviation of the local thresholds in DB are greater than this,
  //  the result is probably bad (number from the paper)
  //double MAX_STD_DB = 5.0;

  // The maximum allowed value, from the paper.
  //double MAX_THRESHOLD_DB = 10.0;

  double threshold_stddev = math::standard_deviation(optimal_tile_thresholds, threshold_mean);
  
  // TODO // Recompute these values in the original DB units.
  //tileThresholdsDb = [rescaleNumber(x, PROC_global_min, PROC_global_max, minVal, maxVal) for x in tileThresholds]
  //threshMeanDb = numpy.mean(tileThresholdsDb)
  //threshStdDb  = numpy.std(tileThresholdsDb, ddof=1)

  std::cout << "Mean of tile thresholds: " << threshold_mean   << std::endl;
  std::cout << "STD  of tile thresholds: " << threshold_stddev << std::endl;
  //print 'Mean of tile thresholds (DB): ' + str(threshMeanDb)
  //print 'STD  of tile thresholds (DB): ' + str(threshStdDb)

  // TODO: Verify that the computed threshold looks reasonable!
  return true;

  //// TODO: Use an alternate method of computing the threshold like they do in the paper!
  //if threshStdDb > MAX_STD_DB:
  //    raise Exception('Failed to compute a good threshold! STD = ' + str(threshStdDb))
  // TODO
  //if threshMeanDb > MAX_THRESHOLD_DB:
  //    threshMean = rescaleNumber(MAX_THRESHOLD_DB, minVal, maxVal, PROC_global_min, PROC_global_max)
  //    print 'Saturating the computed threshold at 10 DB!'

} // End compute_global_threshold

//============================================================================



/** Main function of algorithm from:
      Martinis, Sandro, Jens Kersten, and Andre Twele. 
      "A fully automated TerraSAR-X based flood service." 
      ISPRS Journal of Photogrammetry and Remote Sensing 104 (2015): 203-212.
*/
void sar_martinis(std::string const& input_image_path, 
                  cartography::GdalWriteOptions const& write_options,
                  int tile_size = 512) {

  // TODO: How to specify the ROI?
  
  BBox2i roi = bounding_box(DiskImageView<Sentinel1Type>(input_image_path));
 
  // Load the georeference from the input image
  // - The input image won't be georeferenced unless it goes through gdalwarp.
  cartography::GeoReference georef;
  bool have_georef = cartography::read_georeference(georef, input_image_path);
  if (!have_georef)
    vw_throw(ArgumentErr() << "Failed to read image georeference!");
  georef = crop(georef, roi); // Account for the input ROI
  std::cout << "Read georeference: " << georef << std::endl;
  
  double input_meters_per_pixel = cartography::get_image_meters_per_pixel(roi.width(), roi.height(), georef);
  std::cout << "Computed image pixel resolution in meters: " << input_meters_per_pixel << std::endl;
  
  // Read nodata value
  double nodata_value     = 0;
  bool   has_input_nodata = read_nodata_val(input_image_path, nodata_value);
   
  // Compute the min and max values of the image
  std::cout << "Preprocessing...\n";

  std::string preprocessed_image_path = "preprocessed_image.tif";

  // Apply any needed preprocessing to the image
  ImageViewRef<RadarTypeM> preprocessed_image;
  RadarType global_min, global_max;
  if (has_input_nodata)
    preprocess_sentinel1_image(DiskImageView<Sentinel1Type>(input_image_path), roi, 
                               global_min, global_max, write_options, preprocessed_image_path, preprocessed_image);
  else
    preprocess_sentinel1_image(create_mask(DiskImageView<Sentinel1Type>(input_image_path), nodata_value), roi, 
                               global_min, global_max, write_options, preprocessed_image_path, preprocessed_image);
  
  // Generate vector of BBoxes for each tile in the input image (S+)
  std::vector<std::vector<BBox2i> > large_tile_boxes;
  divide_roi(bounding_box(preprocessed_image), tile_size, large_tile_boxes, false);
  int num_boxes_y = large_tile_boxes.size();
  int num_boxes_x = large_tile_boxes[0].size();
  //printf("Num boxes: %d, %d\n", num_boxes_x, num_boxes_y);
  std::cout << "Computing tile means...\n";

  // For each tile compute the mean value and the standard deviation of the four sub-tiles.
  ImageView<RadarTypeM> tile_means, tile_stddevs; // These are much smaller than the input image
  generate_tile_means(preprocessed_image, tile_size, num_boxes_x, num_boxes_y,
                      tile_means, tile_stddevs);

  std::cout << "Writing DEBUG images...\n";
  block_write_gdal_image("tile_means.tif",   tile_means,   write_options);
  block_write_gdal_image("tile_stddevs.tif", tile_stddevs, write_options);


  // Select the tiles that we will use to compute the optimal global threshold.
  std::vector<Vector2i> kept_tile_indices;
  select_best_tiles(tile_means, tile_stddevs, kept_tile_indices, write_options);

  // Use the selected tiles to compute the optimal image threshold.
  double threshold_mean;
  if (!compute_global_threshold(preprocessed_image, kept_tile_indices, large_tile_boxes,
                                global_min, global_max, threshold_mean)) {
    std::cout << "WARNING: Computed threshold may be inaccurate!\n";
  }
 
  // TODO: When either of the previous steps fail, repeat the earlier steps with the tile size cut in half.
  
  // This will mask the water pixels, setting water pixels to 255, land pixels to 1, and invalid pixels to 0.
  const uint8 WATER_CLASS  = 255;
  const uint8 LAND_CLASS   = 1;
  const uint8 NODATA_CLASS = 0;
  ImageViewRef<RadarTypeM> raw_water = threshold(preprocessed_image, threshold_mean, WATER_CLASS, LAND_CLASS);

  // DEBUG: Apply the initial threshold to the image and save it to disk!
  std::string initial_water_detect_path = "initial_water_detect.tif";
  block_write_gdal_image(initial_water_detect_path,
                         pixel_cast<uint8>(apply_mask(raw_water, NODATA_CLASS)),
                         have_georef, georef,
                         true, NODATA_CLASS, // Choose the nodata value
                         write_options,
                         TerminalProgressCallback("vw", "\t--> Applying initial threshold:"));

  // Get information needed for fuzzy logic results filtering

  // Write out an image containing the water blob size at each pixel, then read it back in
  // as needed to avoid recomputing the expensive blob computations.
  // - In order to parallelize this step, blob computations are approximated.
  
  const double MIN_BLOB_SIZE_METERS = 250.0; 
  const double MAX_BLOB_SIZE_METERS = 1000.0;
  const int    TILE_EXPAND   = 256; // The larger this number, the better the approximation.
  
  uint32 min_blob_size = MIN_BLOB_SIZE_METERS / input_meters_per_pixel;
  uint32 max_blob_size = MAX_BLOB_SIZE_METERS / input_meters_per_pixel;

  std::string blobs_path = "blob_sizes.tif";
  const uint32 BLOBS_NODATA = 0;
  block_write_gdal_image(blobs_path,
                         get_blob_sizes(create_mask_less_or_equal(DiskImageView<uint8>(initial_water_detect_path), LAND_CLASS),
                                        TILE_EXPAND, max_blob_size),
                         have_georef, georef,
                         true, BLOBS_NODATA,
                         write_options,
                         TerminalProgressCallback("vw", "\t--> Counting blob sizes:"));
  // TODO: Fill invalid pixels!
  DiskImageView<uint32> blob_sizes(blobs_path);


  int DEM_STATS_SUBSAMPLE_FACTOR = 10;

  std::cout << "Computing mean of flooded regions...\n";

  // Load a low-res version of our initial water results.
  ImageViewRef<RadarTypeM> low_res_raw_water =  
          copy_mask(subsample(preprocessed_image, DEM_STATS_SUBSAMPLE_FACTOR),
                    subsample(create_mask_less_or_equal(DiskImageView<uint8>(initial_water_detect_path), 
                                                                                           LAND_CLASS), 
                                                                 DEM_STATS_SUBSAMPLE_FACTOR)
                   );
  cartography::GeoReference low_res_georef = resample(georef, 1.0/DEM_STATS_SUBSAMPLE_FACTOR);
  printf("Low res water image is size: %d x %d\n", low_res_raw_water.cols(), low_res_raw_water.rows());

  // Compute mean radar value of pixels under initial water threshold
  // - This is also computed at a lower resolution to increase speed.
  // - Could do full res with a multi-threaded implementation.
  double mean_raw_water_value = mean_channel_value(low_res_raw_water);

  std::cout << "Mean value of flooded regions = " << mean_raw_water_value << std::endl;

  //block_write_gdal_image("low_res_water.tif", low_res_raw_water, 
  //                       have_georef, low_res_georef, true, -9999,
  //                       write_options, TerminalProgressCallback("vw", "\t--> DEBUG:"));


  // TODO: Work out how to load DEM information!
  std::string dem_path = "/home/smcmich1/data/usgs_floods/dem/imgn30w095_13.tif";


  typedef PixelMask<float> DemPixelType;

  // Should be safe to use this as a DEM nodata value!
  double dem_nodata_value = -3.4028234663852886e+38;
  bool have_dem_nodata = read_nodata_val(dem_path, dem_nodata_value);

  DiskImageView<float> dem(dem_path);

  cartography::GeoReference dem_georef;
  if (!cartography::read_georeference(dem_georef, dem_path))
    vw_throw(ArgumentErr() << "Failed to read DEM georeference!");

  // Generate a low-resolution DEM masked by the initial flood detection
  // - This is used to compute image-wide statistics in a more reasonable amount of time
  // - TODO: Fill in holes in the masked DEM
  // TODO: Use the full res dem on disk since we are accessing it at low res?
  ImageView<DemPixelType> low_res_dem = subsample(create_mask(dem, dem_nodata_value), DEM_STATS_SUBSAMPLE_FACTOR);
  cartography::GeoReference low_res_dem_georef = resample(dem_georef, 1.0/DEM_STATS_SUBSAMPLE_FACTOR);


  block_write_gdal_image("low_res_dem.tif",   apply_mask(low_res_dem, dem_nodata_value),
                         have_georef, low_res_dem_georef, have_dem_nodata, dem_nodata_value,
                         write_options, TerminalProgressCallback("vw", "\t--> dem:"));

  std::cout << "low georef = " << low_res_georef << std::endl;
  std::cout << "DEM georef = " << low_res_dem_georef << std::endl;

  ImageViewRef<PixelMask<float> > low_res_dem_in_image_coords = 
    cartography::geo_transform(low_res_dem, low_res_dem_georef, low_res_georef,
                               low_res_raw_water.cols(), low_res_raw_water.rows(), ConstantEdgeExtension());
  ImageViewRef<PixelMask<float> > dem_in_image_coords = 
    cartography::geo_transform(create_mask(dem, dem_nodata_value), dem_georef, georef,
                               preprocessed_image.cols(), preprocessed_image.rows(), ConstantEdgeExtension());

//  block_write_gdal_image("geotrans_dem.tif",
//                          apply_mask(dem_in_image_coords, dem_nodata_value),
//                         have_georef, low_res_georef, true, dem_nodata_value,
//                         write_options, TerminalProgressCallback("vw", "\t--> geotrans:"));

  // Now go through and compute statistics across the water covered locations of the DEM
  //typedef FunctorMaskWrapper<StdDevAccumulator<float>, PixelMask<float> > MaskedStdDevFunctor;
  StdDevAccumulator<float> stddev_functor;
  FunctorMaskWrapper<StdDevAccumulator<float>, PixelMask<float> > dem_stats_functor(stddev_functor);
  for_each_pixel(copy_mask(low_res_dem_in_image_coords, low_res_raw_water), dem_stats_functor);
  
  float mean_water_height   = dem_stats_functor.child().value();
  float stddev_water_height = dem_stats_functor.child().mean();

  std::cout << "Mean height of flooded regions = " << mean_water_height 
            << ", and stddev = " << stddev_water_height << std::endl;

    
  // Compute fuzzy classifications on four categories
  typedef PixelMask<float> FuzzyPixelType;
  typedef FuzzyMembershipSFunctor<RadarTypeM> FuzzyFunctorS;
  typedef FuzzyMembershipZFunctor<RadarTypeM> FuzzyFunctorZ;

 
  // SAR
  FuzzyFunctorZ radar_fuzz_functor(mean_raw_water_value, threshold_mean);
  ImageViewRef<FuzzyPixelType> radar_fuzz = per_pixel_view(preprocessed_image, radar_fuzz_functor);

  // Elevation
  // - The max value looks a little weird but it comes straight from the paper.
  const double high_height = mean_water_height + stddev_water_height*(stddev_water_height + 3.5);
  FuzzyFunctorZ height_fuzz_functor(mean_water_height, high_height);
  ImageViewRef<FuzzyPixelType> height_fuzz = per_pixel_view(dem_in_image_coords, height_fuzz_functor);
  
  // Slope
  const double degrees_low  = 0;
  const double degrees_high = 15;
  FuzzyFunctorZ slope_fuzz_functor(degrees_low, degrees_high);
  ImageViewRef<FuzzyPixelType> slope_fuzz = per_pixel_view(get_angle(compute_normals(dem_in_image_coords, 1.0, 1.0)), slope_fuzz_functor);
  
  // Body size
  
  FuzzyFunctorS blob_fuzz_functor(min_blob_size, max_blob_size);
  ImageViewRef<FuzzyPixelType> blob_fuzz = per_pixel_view(blob_sizes, blob_fuzz_functor);

/*
  block_write_gdal_image("radar_fuzz.tif", apply_mask(radar_fuzz, dem_nodata_value),
                         have_georef, georef, true, dem_nodata_value,
                         write_options, TerminalProgressCallback("vw", "\t--> radar_fuzz:"));
  block_write_gdal_image("height_fuzz.tif", apply_mask(height_fuzz, dem_nodata_value),
                         have_georef, georef, true, dem_nodata_value,
                         write_options, TerminalProgressCallback("vw", "\t--> height_fuzz:"));
  block_write_gdal_image("slope_fuzz.tif", apply_mask(slope_fuzz, dem_nodata_value),
                         have_georef, georef, true, dem_nodata_value,
                         write_options, TerminalProgressCallback("vw", "\t--> slope_fuzz:"));
  block_write_gdal_image("blob_fuzz.tif", apply_mask(blob_fuzz, dem_nodata_value),
                         have_georef, georef, true, dem_nodata_value,
                         write_options, TerminalProgressCallback("vw", "\t--> blob_fuzz:"));
*/
                         
  // Defuzz the four fuzzy classifiers and compare to a fixed threshold in the 0-1 range.
  typedef DefuzzFunctor<FuzzyPixelType, FuzzyPixelType, FuzzyPixelType, FuzzyPixelType> DefuzzFunctorType;
  ImageViewRef<FuzzyPixelType> defuzzed = per_pixel_view(radar_fuzz, height_fuzz, slope_fuzz, blob_fuzz, DefuzzFunctorType());

/*                         
  block_write_gdal_image("defuzzed.tif", apply_mask(defuzzed, dem_nodata_value),
                         have_georef, georef, true, dem_nodata_value,
                         write_options, TerminalProgressCallback("vw", "\t--> Defuzz:"));
*/

  // Perform two-level flood fill of the defuzzed image and write it to disk.
  // - The mask is added back in at this point.
  const double final_flood_threshold = 0.6;
  const double water_grow_threshold  = 0.45;
  std::string output_path = "radar_final_output.tif";
  block_write_gdal_image(output_path,
                         apply_mask(
                           copy_mask(
                             two_threshold_fill(defuzzed, TILE_EXPAND, final_flood_threshold, 
                                                water_grow_threshold, LAND_CLASS, WATER_CLASS),
                             create_mask(DiskImageView<uint8>(initial_water_detect_path, NODATA_CLASS))
                           ),
                           NODATA_CLASS
                         ),      
                         true, georef,
                         true, NODATA_CLASS,
                         write_options,
                         TerminalProgressCallback("vw", "\t--> Generating final output:"));


  // Clean up temporary image files
  std::remove(preprocessed_image_path.c_str());

} // End function sar_martinis



/*

  Stopwatch timer;
  timer.start();
  timer.stop();
  std::cout << "TT time = " << timer.elapsed_seconds() << std::endl;

Test region:
Upper Left  ( -95.5011734,  30.5498188) 
Lower Left  ( -95.5011734,  29.0013302) 
Upper Right ( -94.3983680,  30.5498188) 
Lower Right ( -94.3983680,  29.0013302) 
Center      ( -94.9497707,  29.7755745) 

imgn30w093_13.tif
Upper Left  ( -93.0005556,  30.0005556) 
Lower Left  ( -93.0005556,  28.9994444) 
Upper Right ( -91.9994444,  30.0005556) 
Lower Right ( -91.9994444,  28.9994444) 
Center      ( -92.5000000,  29.5000000) 

imgn30w095_13.tif
Upper Left  ( -95.0005556,  30.0005556) 
Lower Left  ( -95.0005556,  28.9994444) 
Upper Right ( -93.9994444,  30.0005556) 
Lower Right ( -93.9994444,  28.9994444) 
Center      ( -94.5000000,  29.5000000) 

imgn30w096_13.tif
Upper Left  ( -96.0005556,  30.0005556) 
Lower Left  ( -96.0005556,  28.9994444) 
Upper Right ( -94.9994444,  30.0005556) 
Lower Right ( -94.9994444,  28.9994444) 
Center      ( -95.5000000,  29.5000000) 

imgn31w093_13.tif
Upper Left  ( -93.0005556,  31.0005556) 
Lower Left  ( -93.0005556,  29.9994444) 
Upper Right ( -91.9994444,  31.0005556) 
Lower Right ( -91.9994444,  29.9994444) 
Center      ( -92.5000000,  30.5000000) 

imgn31w094_13.tif
Upper Left  ( -94.0005556,  31.0005556) 
Lower Left  ( -94.0005556,  29.9994444) 
Upper Right ( -92.9994444,  31.0005556) 
Lower Right ( -92.9994444,  29.9994444) 
Center      ( -93.5000000,  30.5000000) 

imgn31w095_13.tif
Upper Left  ( -95.0005556,  31.0005556) 
Lower Left  ( -95.0005556,  29.9994444) 
Upper Right ( -93.9994444,  31.0005556) 
Lower Right ( -93.9994444,  29.9994444) 
Center      ( -94.5000000,  30.5000000) 

imgn31w096_13.tif
Upper Left  ( -96.0005556,  31.0005556) 
Lower Left  ( -96.0005556,  29.9994444) 
Upper Right ( -94.9994444,  31.0005556) 
Lower Right ( -94.9994444,  29.9994444) 
Center      ( -95.5000000,  30.5000000) 
*/




}} // end namespace vw/radar
#endif

