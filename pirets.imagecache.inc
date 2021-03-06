<?php

/**
 * @file
 * Home of the fetch-the-RETS-image-with-ImageCache epic kludge.
 */

/**
 * Fetch images from RETS servers and save to disk. ImageCache action.
 *
 * When this is called, assuming the property node is created correctly, the
 * image that is actually attached to the property is a 1 pixel square white
 * JPEG. This ImageCache action checks to see if "this" image is still in that
 * state (its filesize is 304 bytes). If so, it fetches the real image from RETS
 * and replaces the placeholder with the proper image, both on the disk and in
 * memory (so that the next ImageCache action can manipulate it). This hack
 * allows for "just-in-time" image fetching - images aren't fetched until
 * they're actually about to be seen, and certainly not at the same time as
 * property listings are downloaded or the property node is created.
 */
function pirets_fetch_image(&$image, $data) {
  // Does it look like a placholder JPEG?
  if (preg_match('~/pirets_img/(.*/)*((\d+)-(\d+)\.jpeg)$~', $image->source, $matches) && $image->info['file_size'] === 304) {
    // Try to fetch the picture
    module_load_include('inc', 'pirets', 'pirets.connect');
    $result = pirets_query_prop_img($matches[3], $matches[4]);
    error_log(print_r($result, TRUE));
    if ($result->response_code !== 200) {
      watchdog('pirets', 'An attempt to fetch the photo @id failed.', array('@id' => "{$matches[3]}:{$matches[4]}"), WATCHDOG_ERROR);
      return FALSE;
    }
    // Write the image
    file_put_contents($image->source, $result->data_raw);
    // Update {files}
    $record = array(
      'filename' => $matches[2],
      'filesize' => isset($data) ? strlen($result->data_raw) : filesize($dest),
    );
    drupal_write_record('files', $record, 'filename');
    // Reload $image so the new image can be properly processed by further
    // ImageCache actions
    // We can't just say $image = wholesale because ImageCache seems built upon
    // a house of cards that is PHP's quirky manner in which it passes objects
    // (by reference, even if that's not explicitly stated).
    // http://drupal.org/node/623770
    $new_img = imageapi_image_open($image->source);
    $image->info = $new_img->info;
    $image->resource = $new_img->resource;
  }
  return TRUE;
}

/**
 * Show options for the pirets_fetch_image() action (of which there are none, so tell the user this). ImageCache form callback.
 */
function pirets_fetch_form($action) {
  return array(
    'notice' => array(
      '#type' => 'markup',
      '#value' => t('<p><em>This ImageCache action has no options.</em></p>'),
    ),
  );
}
