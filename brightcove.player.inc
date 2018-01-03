<?php

/**
 * Page callback for 'admin/config/media/brightcove/players'.
 *
 * @return array
 */
function brightcove_admin_players() {
  $build = [
    '#theme' => 'table',
    '#header' => [
      t('Name'),
      t('Description'),
      t('Client'),
      t('Operations'),
    ],
    '#rows' => [],
    '#emtpy' => t('You don\'t have any players configured'),
  ];

  foreach (entity_load('brightcove_client') as $client) {
    /** @var \Brightcove\API\PM $pm */
    list(,,$pm) = brightcove_create_classes($client);
    $build['#rows'] += brightcove_try(function() use($client, $pm) {
      $items = [];
      $players = $pm->listPlayers();
      foreach ($players->getItems() as $player) {
        $items[$player->getId()] = [
          l($player->getName(), "admin/config/media/brightcove/players/{$client->bcid}/{$player->getId()}"),
          $player->getDescription(),
          $client->label,
          l(t('Edit'), "admin/config/media/brightcove/players/{$client->bcid}/{$player->getId()}/edit") . "\t" .
            l(t('Delete'), "admin/config/media/brightcove/players/{$client->bcid}/{$player->getId()}/delete"),
        ];
      }

      return $items;
    });

  }

  return $build;
}

/**
 * Page callback for 'admin/config/media/brightcove/players/%/%/view'.
 *
 * @param string $bcid
 * @param string $player_id
 *
 * @return array
 */
function brightcove_admin_player_view($bcid, $player_id) {
  $client = brightcove_client_load($bcid);
  if (!$client) {
    drupal_not_found();
  }

  /** @var \Brightcove\API\PM $pm */
  list(,,$pm) = brightcove_create_classes($client);

  $build = brightcove_try(function() use($bcid, $player_id, $pm) {
    $build = [];

    $player = $pm->getPlayer($player_id);

    $preview_url = $player
      ->getBranches()
      ->getPreview()
      ->getPreviewUrl()
    ;
    $width = $player
      ->getBranches()
      ->getPreview()
      ->getConfiguration()
      ->getStudioConfiguration()
      ->getPlayer()
      ->getWidth()
    ;
    $height = $player
      ->getBranches()
      ->getPreview()
      ->getConfiguration()
      ->getStudioConfiguration()
      ->getPlayer()
      ->getHeight()
    ;

    $build['widget'] = [
      '#theme' => 'brightcove_player_preview',
      '#preview_url' => $preview_url,
    ];

    if ($width) {
      $build['widget']['#width'] = $width;
    }

    if ($height) {
      $build['widget']['#height'] = $height;
    }

    $build['embeds'] = [
      '#title' => t('Embeds'),
      '#theme' => 'item_list',
      '#items' => array_map(function(\Brightcove\Object\Player\Embed $embed) use($bcid, $player_id) {
        return l($embed->getName(), "admin/config/media/brightcove/players/{$bcid}/{$player_id}/{$embed->getId()}");
      }, $pm->listEmbeds($player_id)->getItems()),
    ];

    return $build;
  });

  if (!$build) {
    drupal_not_found();
  }

  return $build;
}

/**
 * Form callback for 'admin/config/media/brightcove/players/{%/%/edit,add}'.
 *
 * @param array $form
 * @param array $form_state
 * @param string|null $bcid
 * @param string|null $player_id
 *   Must be set when $bcid is set.
 *
 * @return array
 */
function brightcove_admin_player_edit_form(array $form, array &$form_state, $bcid = NULL, $player_id = NULL) {
  if ($bcid) {
    $form['bcid'] = [
      '#type' => 'value',
      '#value' => $bcid,
    ];
  }
  else {
    $default_client = brightcove_client_load_or_default();
    $form['bcid'] = [
      '#type' => 'select',
      '#title' => t('API Client'),
      '#options' => array_map(function($client) {
        return $client->label;
      }, entity_load('brightcove_client')),
      '#default_value' => $default_client ? $default_client->bcid : NULL,
    ];
  }

  /** @var \Brightcove\Object\Player\Branch\Configuration\Configuration $configuration */
  $configuration = NULL;
  /** @var \Brightcove\Object\Player\Player $player */
  $player = NULL;

  if ($player_id) {
    $client = brightcove_client_load($bcid);
    /** @var \Brightcove\API\PM $pm */
    list(,,$pm) = brightcove_create_classes($client);
    $player = $pm->getPlayer($player_id);

    $configuration = $player->getBranches()->getPreview()->getConfiguration();
  }
  else {
    $configuration = _brightcove_admin_empty_configuration();
  }

  $form['player_id'] = [
    '#type' => 'value',
    '#value' => $player_id,
  ];

  $form['name'] = [
    '#type' => 'textfield',
    '#title' => t('Name'),
    '#default_value' => $player ? $player->getName() : '',
    '#required' => TRUE,
  ];

  $form['description'] = [
    '#type' => 'textarea',
    '#title' => t('Description'),
    '#default_value' => $player ? $player->getDescription() : '',
  ];

  $form = _brightcove_admin_player_configuration_form($form, $configuration);

  $form['submit'] = [
    '#type' => 'submit',
    '#value' => t('Save'),
  ];

  return $form;
}

/**
 * Submit handler for brightcove_admin_player_edit_form().
 *
 * @param array $form
 * @param array $form_state
 */
function brightcove_admin_player_edit_form_submit(array $form, array &$form_state) {
  $bcid = $form_state['values']['bcid'];
  $player_id = $form_state['values']['player_id'];
  $name = $form_state['values']['name'];
  $description = $form_state['values']['description'];

  /** @var \Brightcove\Object\Player\Branch\Configuration\Configuration $configuration */
  $configuration = NULL;
  $client = brightcove_client_load($bcid);
  /** @var \Brightcove\API\PM $pm */
  list(, , $pm) = brightcove_create_classes($client);

  if ($player_id) {
    $player = $pm->getPlayer($player_id);

    $configuration = $player->getBranches()->getPreview()->getConfiguration();
  }
  else {
    $configuration = _brightcove_admin_empty_configuration();
  }

  _brightcove_admin_apply_on_configuration($form_state, $configuration);

  $success = brightcove_try(function() use(&$player_id, $pm, $configuration, $name, $description) {
    if ($player_id) {
      $update_data = new \Brightcove\Object\Player\UpdateData();
      $update_data->setName($name);
      $update_data->setDescription($description);

      $pm->updatePlayer($player_id, $update_data);

      $pm->updatePlayerConfigurationBranch($player_id, $configuration);
    }
    else {
      $create_data = new \Brightcove\Object\Player\CreateData();
      $create_data->setName($name);
      $create_data->setDescription($description);
      $create_data->setConfiguration($configuration);

      /** @var \Brightcove\Object\Player\CreateResult $result */
      $result = $pm->createPlayer($create_data);
      $player_id = $result->getId();
    }

    return TRUE;
  }, function() use($player_id) {
    if ($player_id) {
      drupal_set_message(t('Failed to update player'));
    }
    else {
      drupal_set_message(t('Failed to create player.'), 'error');
    }

    return FALSE;
  });

  if ($success) {
    $form_state['redirect'] = "admin/config/media/brightcove/players/{$bcid}/{$player_id}";
  }
}

/**
 * Form callback for 'admin/config/media/brightcove/players/%/%/delete'.
 *
 * @param array $form
 * @param array $form_state
 * @param string $bcid
 * @param string $player_id
 *
 * @return mixed
 */
function brightcove_admin_player_delete_form(array $form, array &$form_state, $bcid, $player_id) {
  $form['bcid'] = [
    '#type' => 'value',
    '#value' => $bcid,
  ];

  $form['player_id'] = [
    '#type' => 'value',
    '#value' => $player_id,
  ];

  return confirm_form(
    $form,
    'Are you sure that you want to delete this player?',
    'admin/config/media/brightcove/players'
  );
}

/**
 * Submit handler for brightcove_admin_player_delete_form().
 *
 * @param array $form
 * @param array $form_state
 */
function brightcove_admin_player_delete_form_submit(array $form, array &$form_state) {
  $bcid = $form_state['values']['bcid'];
  $player_id = $form_state['values']['player_id'];

  $client = brightcove_client_load($bcid);
  /** @var \Brightcove\API\PM $pm */
  list(,,$pm) = brightcove_create_classes($client);

  $form_state['redirect'] = 'admin/config/media/brightcove/players';

  brightcove_try(function() use($pm, $player_id) {
    $pm->deletePlayer($player_id);
    drupal_set_message(t('Player deleted.'));
  }, function() {
    drupal_set_message(t('Failed to delete player.'), 'error');
  });
}

/**
 * Form callback for 'admin/config/media/brightcove/players/%/%/publish'.
 *
 * @param array $form
 * @param array $form_state
 * @param string $bcid
 * @param string $player_id
 *
 * @return array
 */
function brightcove_admin_player_publish_form(array $form, array &$form_state, $bcid, $player_id) {
  $form['bcid'] = [
    '#type' => 'value',
    '#value' => $bcid,
  ];

  $form['player_id'] = [
    '#type' => 'value',
    '#value' => $player_id,
  ];

  $form['comment'] = [
    '#type' => 'textfield',
    '#title' => t('Comment (optional)'),
  ];

  $form['publish'] = [
    '#type' => 'submit',
    '#value' => t('Publish'),
  ];

  return $form;
}

/**
 * Submit handler for brightcove_admin_player_publish_form().
 *
 * @param array $form
 * @param array $form_state
 */
function brightcove_admin_player_publish_form_submit(array $form, array &$form_state) {
  $bcid = $form_state['values']['bcid'];
  $player_id = $form_state['values']['player_id'];
  $comment = $form_state['values']['comment'];

  $client = brightcove_client_load($bcid);
  /** @var \Brightcove\API\PM $pm */
  list(,,$pm) = brightcove_create_classes($client);

  $form_state['redirect'] = 'admin/config/media/brightcove/players';

  brightcove_try(function() use($pm, $player_id, $comment) {
    $pm->publishPlayer($player_id, $comment);
    drupal_set_message(t('Player published.'));
  }, function() {
    drupal_set_message(t('Failed to publish player.'), 'error');
  });
}

/**
 * Page callback for 'admin/config/media/brightcove/players/%/%/%'.
 *
 * @param string $bcid
 * @param string $player_id
 * @param string $embed_id
 *
 * @return array
 */
function brightcove_admin_embed_view($bcid, $player_id, $embed_id) {
  $client = brightcove_client_load($bcid);
  if (!$client) {
    drupal_not_found();
  }

  /** @var \Brightcove\API\PM $pm */
  list(,,$pm) = brightcove_create_classes($client);

  $build = brightcove_try(function() use($player_id, $embed_id, $pm) {
    $build = [];

    $embed = $pm->getEmbed($player_id, $embed_id);

    $build['widget'] = [
      '#markup' => $embed->getEmbedCode(),
    ];

    return $build;
  });

  if (!$build) {
    drupal_not_found();
  }

  return $build;
}

function brightcove_admin_embed_preview($bcid, $player_id, $embed_id) {
  // Preview the child player before publishing it.
  $client = brightcove_client_load($bcid);
  if (!$client) {
    drupal_not_found();
  }

  list(,,$pm) = brightcove_create_classes($client);

  $build = brightcove_try(function() use($player_id, $embed_id, $pm) {
    $build = [];

    $embed = $pm->getEmbed($player_id, $embed_id);

    $build['widget'] = [
      '#markup' => $embed->getPreviewEmbedCode(),
    ];

    return $build;
  });

  if (!$build) {
    drupal_not_found();
  }

  $elements = drupal_get_form('brightcove_admin_embed_publish_form', $bcid, $player_id, $embed_id);
  $build['widget']['#markup'] .= drupal_render($elements);

  return $build;
}

/**
 * Form callback for 'admin/config/media/brightcove/players/%/%/{%/edit,add-embed}'.
 *
 * @param array $form
 * @param array $form_state
 * @param string $bcid
 * @param string $player_id
 * @param string|null $embed_id
 *
 * @return array
 */
function brightcove_admin_embed_edit_form(array $form, array &$form_state, $origin, $bcid, $player_id, $embed_id = NULL) {
  /** @var \Brightcove\Object\Player\Branch\Configuration\Configuration $configuration */
  $configuration = NULL;
  /** @var \Brightcove\Object\Player\Embed $embed */
  $embed = NULL;

  $client = brightcove_client_load($bcid);
  /** @var \Brightcove\API\PM $pm */
  list(,,$pm) = brightcove_create_classes($client);
  if ($embed_id) {
    $embed = $pm->getEmbed($player_id, $embed_id);
    $configuration = $embed->getBranches()->getPreview()->getConfiguration();
  }
  else {
    $player = $pm->getPlayer($player_id);
    $configuration = $player->getBranches()->getMaster()->getConfiguration();
  }

  $form['bcid'] = [
    '#type' => 'value',
    '#value' => $bcid,
  ];

  $form['player_id'] = [
    '#type' => 'value',
    '#value' => $player_id,
  ];

  $form['embed_id'] = [
    '#type' => 'value',
    '#value' => $embed_id,
  ];

  $form['origin'] = [
    '#type' => 'value',
    '#value' => $origin,
  ];

  $form['embed_name'] = [
    '#type' => 'textfield',
    '#title' => t('Name'),
    '#default_value' => $embed ? $embed->getBranches()->getMaster()->getConfiguration()->getEmbedName() : '',
    '#required' => TRUE,
  ];

  $form = _brightcove_admin_player_configuration_form($form, $configuration ?: new \Brightcove\Object\Player\Branch\Configuration\Configuration());

  $form['submit'] = [
    '#type' => 'submit',
    '#value' => t('Save'),
  ];

  return $form;
}

/**
 * Submit handler for brightcove_admin_embed_edit_form().
 *
 * @param array $form
 * @param array $form_state
 */
function brightcove_admin_embed_edit_form_submit(array $form, array &$form_state) {
  $bcid = $form_state['values']['bcid'];
  $player_id = $form_state['values']['player_id'];
  $embed_id = $form_state['values']['embed_id'];
  $name = $form_state['values']['embed_name'];
  $origin = $form_state['values']['origin'];

  /** @var \Brightcove\Object\Player\Branch\Configuration\Configuration $configuration */
  $configuration = NULL;
  $client = brightcove_client_load($bcid);
  /** @var \Brightcove\API\PM $pm */
  list(,,$pm) = brightcove_create_classes($client);

  if ($embed_id) {
    $embed = $pm->getEmbed($player_id, $embed_id);

    $configuration = $embed->getBranches()->getPreview()->getConfiguration();
  }
  else {
    $configuration = _brightcove_admin_empty_configuration();
  }

  _brightcove_admin_apply_on_configuration($form_state, $configuration);

  $success = brightcove_try(function() use(&$embed_id, $player_id, $pm, $configuration, $name) {
    if ($embed_id) {
      $pm->updateEmbedConfigurationBranch($player_id, $embed_id, $configuration);
    }
    else {
      $result = $pm->createEmbed($player_id, $configuration);
      $embed_id = $result->getId();
    }

    return TRUE;
  }, function() use($embed_id) {
    if ($embed_id) {
      drupal_set_message(t('Embed updated.'));
    }
    else {
      drupal_set_message(t('Failed to create embed'), 'error');
    }

    return FALSE;
  });

  if ($success) {
    switch ($origin) {
      case 'add-embed':
        $form_state['redirect'] = "admin/config/media/brightcove/players/{$bcid}/{$player_id}/{$embed_id}/view";
        break;
      case 'edit':
        $form_state['redirect'] = "admin/config/media/brightcove/players/{$bcid}/{$player_id}/{$embed_id}/preview";
        break;
      default:
        $form_state['redirect'] = "admin/config/media/brightcove/players/{$bcid}/{$player_id}/{$embed_id}";
        break;
    }
  }
}

/**
 * Form callback for 'admin/config/media/brightcove/players/%/%/%/delete'.
 *
 * @param array $form
 * @param array $form_state
 * @param string $bcid
 * @param string $player_id
 * @param string $embed_id
 *
 * @return mixed
 */
function brightcove_admin_embed_delete_form(array $form, array &$form_state, $bcid, $player_id, $embed_id) {
  $form['bcid'] = [
    '#type' => 'value',
    '#value' => $bcid,
  ];

  $form['player_id'] = [
    '#type' => 'value',
    '#value' => $player_id,
  ];

  $form['embed_id'] = [
    '#type' => 'value',
    '#value' => $embed_id,
  ];

  return confirm_form(
    $form,
    'Are you sure that you want to delete this embed?',
    "admin/config/media/brightcove/players/{$bcid}/{$player_id}"
  );
}

/**
 * Submit handler for brightcove_admin_embed_delete_form().
 *
 * @param array $form
 * @param array $form_state
 */
function brightcove_admin_embed_delete_form_submit(array $form, array &$form_state) {
  $bcid = $form_state['values']['bcid'];
  $player_id = $form_state['values']['player_id'];
  $embed_id = $form_state['values']['embed_id'];

  $client = brightcove_client_load($bcid);
  /** @var \Brightcove\API\PM $pm */
  list(,,$pm) = brightcove_create_classes($client);

  $form_state['redirect'] = "admin/config/media/brightcove/players/{$bcid}/{$player_id}";

  brightcove_try(function() use ($pm, $player_id, $embed_id) {
    $pm->deleteEmbed($player_id, $embed_id);
    drupal_set_message(t('Embed deleted.'));
  }, function() {
    drupal_set_message(t('Failed to delete embed'), 'error');
  });
}

/**
 * Form callback for 'admin/config/media/brightcove/players/%/%/%/publish'.
 *
 * @param array $form
 * @param array $form_state
 * @param string $bcid
 * @param string $player_id
 * @param string $embed_id
 *
 * @return array
 */
function brightcove_admin_embed_publish_form(array $form, array &$form_state, $bcid, $player_id, $embed_id) {
  $form['bcid'] = [
    '#type' => 'value',
    '#value' => $bcid,
  ];

  $form['player_id'] = [
    '#type' => 'value',
    '#value' => $player_id,
  ];

  $form['embed_id'] = [
    '#type' => 'value',
    '#value' => $embed_id,
  ];

  $form['comment'] = [
    '#type' => 'textfield',
    '#title' => t('Comment (optional)'),
  ];

  $form['publish'] = [
    '#type' => 'submit',
    '#value' => t('Publish'),
  ];

  return $form;
}

/**
 * Submit handler for brightcove_admin_embed_publish_form().
 *
 * @param array $form
 * @param array $form_state
 */
function brightcove_admin_embed_publish_form_submit(array $form, array &$form_state) {
  $bcid = $form_state['values']['bcid'];
  $player_id = $form_state['values']['player_id'];
  $embed_id = $form_state['values']['embed_id'];
  $comment = $form_state['values']['comment'];

  $client = brightcove_client_load($bcid);
  /** @var \Brightcove\API\PM $pm */
  list(,,$pm) = brightcove_create_classes($client);

  $form_state['redirect'] = "admin/config/media/brightcove/players/{$bcid}/{$player_id}";

  brightcove_try(function() use($pm, $player_id, $embed_id, $comment) {
    $pm->publishEmbed($player_id, $embed_id, $comment);
    drupal_set_message(t('Embed published.'));
  }, function() {
    drupal_set_message(t('Failed to publish embed.'), 'error');
  });
}

/**
 * Creates the form elements for a Configuration object.
 *
 * @param array $form
 * @param \Brightcove\Object\Player\Branch\Configuration\Configuration $configuration
 *
 * @return array
 */
function _brightcove_admin_player_configuration_form(array $form, \Brightcove\Object\Player\Branch\Configuration\Configuration $configuration) {
  $form['autoadvance'] = [
    '#type' => 'textfield',
    '#title' => t('Autoadvance'),
    '#default_value' => $configuration->isAutoadvance(),
    '#description' => t('autoadvance setting if using playlists: 0 for no wait; a number for seconds between advance; empty to cancel autoadvance'),
  ];

  $form['autoplay'] = [
    '#type' => 'checkbox',
    '#title' => t('Autoplay'),
    '#default_value' => $configuration->isAutoplay(),
    '#description' => t('indicates player should play video immediately, on platforms that allow this'),
  ];

  $form['css'] = [
    '#type' => 'fieldset',
    '#title' => t('CSS'),
    '#collapsible' => FALSE,
  ];

  $form['css']['controlBarColor'] = [
    '#type' => 'textfield',
    '#title' => t('Control bar color'),
    '#default_value' => $configuration->getCss() ? $configuration->getCss()->getControlBarColor() : NULL,
    '#description' => t('background color of control bar'),
  ];

  $form['css']['controlColor'] = [
    '#type' => 'textfield',
    '#title' => t('Control color'),
    '#default_value' => $configuration->getCss() ? $configuration->getCss()->getControlColor() : NULL,
    '#description' => t('color of buttons and text in control bar'),
  ];

  $form['css']['progressColor'] = [
    '#type' => 'textfield',
    '#title' => t('Progress bar color'),
    '#default_value' => $configuration->getCss() ? $configuration->getCss()->getProgressColor() : NULL,
    '#description' => t('color of progress bar'),
  ];

  $form['fullscreenControl'] = [
    '#type' => 'checkbox',
    '#title' => t('Fullscreen control'),
    '#default_value' => $configuration->isFullscreenControl(),
    '#description' => t('indicates whether the fullscreen control should be shown in the control bar'),
  ];

  $form['languages'] = [
    '#type' => 'textfield',
    '#title' => t('Languages'),
    '#default_value' => implode(', ', $configuration->getLanguages() ?: []),
    '#description' => t('languages (2 letter abbreviations) you wish your player to support, separated by a comma'),
  ];

  $form['loop'] = [
    '#type' => 'checkbox',
    '#title' => t('Loop'),
    '#default_value' => $configuration->isLoop(),
    '#description' => t('indicates if the video should play over as soon as it ends'),
  ];

  $form['player'] = [
    '#type' => 'fieldset',
    '#title' => t('Player'),
    '#collapsible' => FALSE,
  ];

  $form['player']['inactive'] = [
    '#type' => 'checkbox',
    '#title' => t('Inactive'),
    '#default_value' => $configuration->getPlayer() ? $configuration->getPlayer()->isInactive() : FALSE,
    '#description' => t('indicates if a player is inactive'),
  ];

  $form['playlist'] = [
    '#type' => 'checkbox',
    '#title' => t('Playlist'),
    '#default_value' => $configuration->isPlaylist(),
    '#description' => t('indicates if a playlist should be used'),
  ];

  $form['playOnSelect'] = [
    '#type' => 'checkbox',
    '#title' => t('Play on select'),
    '#default_value' => $configuration->isPlayOnSelect(),
    '#description' => t('indicates if a video loaded from a playlist should play on load'),
  ];

  $form['preload'] = [
    '#type' => 'radios',
    '#title' => t('Preload'),
    '#options' => [
      'auto' => t('Auto'),
      'metadata' => t('Metadata'),
      'none' => t('None'),
    ],
    '#default_value' => $configuration->getPreload() ?: 'auto',
    '#description' => t('informs the browser whether or not the video data should begin downloading as soon as the video tag is loaded'),
  ];

  $form['resources'] = [
    '#type' => 'fieldset',
    '#title' => t('Resources'),
    '#collapible' => FALSE,
  ];

  $form['resources']['scripts'] = [
    '#type' => 'textarea',
    '#title' => t('Scripts'),
    '#default_value' => implode(PHP_EOL, $configuration->getScripts() ?: []),
    '#description' => t('URLs to JavaScript files that should be included with the player; each URL in a new line'),
  ];

  $form['resources']['stylesheets'] = [
    '#type' => 'textarea',
    '#title' => t('Stylesheets'),
    '#default_value' => implode(PHP_EOL, $configuration->getStylesheets() ?: []),
    '#description' => t('URLs to CSS files that should be included with the player; each URL in a new line'),
  ];

  $skin = $configuration->getSkin();
  if ($skin === NULL) {
    $skin = TRUE;
  }
  if (is_bool($skin)) {
    $skin = $skin ? '1': '0';
  }

  $form['skin'] = [
    '#type' => 'radios',
    '#title' => t('Skin'),
    '#options' => [
      '1' => t('Default skin'),
      '0' => t('Custom skin'),
      'graphite' => t('Legacy skin'),
    ],
    '#default_value' => $skin,
    '#description' => t('indicates is the default look-and-feel will be used with the player'),
  ];

  $form['studio_configuration'] = [
    '#type' => 'fieldset',
    '#title' => t('Playlist information normally set in Studio'),
    '#collapsible' => FALSE,
  ];

  $form['studio_configuration']['adjusted'] = [
    '#type' => 'checkbox',
    '#title' => t('Adjusted'),
    '#default_value' => $configuration->getStudioConfiguration() && $configuration->getStudioConfiguration()->getPlayer() ?
      $configuration->getStudioConfiguration()->getPlayer()->isAdjusted() : NULL,
    '#description' => t('indicates if player dimensions should be adjusted for playlist'),
  ];

  $form['studio_configuration']['height'] = [
    '#type' => 'textfield',
    '#title' => t('Height'),
    '#default_value' => $configuration->getStudioConfiguration() && $configuration->getStudioConfiguration()->getPlayer() ?
      $configuration->getStudioConfiguration()->getPlayer()->getHeight() : NULL,
    '#description' => t('player height when displayed with playlist; if the height and width are not assigned values, the sizes of the player and playlist are automatically adjusted'),
  ];

  $form['studio_configuration']['width'] = [
    '#type' => 'textfield',
    '#title' => t('Width'),
    '#default_value' => $configuration->getStudioConfiguration() && $configuration->getStudioConfiguration()->getPlayer() ?
      $configuration->getStudioConfiguration()->getPlayer()->getWidth() : NULL,
    '#description' => t('player width when displayed with playlist; if the height and width are not assigned values, the sizes of the player and playlist are automatically adjusted'),
  ];

  return $form;
}

/**
 * Applies a configuration form state to a Configuration object.
 *
 * @param array $form_state
 * @param \Brightcove\Object\Player\Branch\Configuration\Configuration $configuration
 */
function _brightcove_admin_apply_on_configuration(array &$form_state, \Brightcove\Object\Player\Branch\Configuration\Configuration $configuration) {
  $v = &$form_state['values'];

  if (isset($v['embed_name'])) {
    $configuration->setEmbedName($v['embed_name']);
  }

  $configuration->setAutoadvance(is_numeric($v['autoadvance']) ? $v['autoadvance'] : NULL);
  $configuration->setAutoplay((bool) $v['autoplay']);
  if (!$configuration->getCss()) {
    $configuration->setCss(new \Brightcove\Object\Player\Branch\Configuration\CSS());
  }
  else {
    $configuration->fieldChanged('css');
  }
  $configuration->getCss()->setControlBarColor($v['controlBarColor']);
  $configuration->getCss()->setControlColor($v['controlColor']);
  $configuration->getCss()->setProgressColor($v['progressColor']);
  $configuration->setFullscreenControl((bool) $v['fullscreenControl']);
  $configuration->setLanguages(array_map('trim', explode(',', $v['languages'])));
  $configuration->setLoop((bool) $v['loop']);
  if (!$configuration->getPlayer()) {
    $configuration->setPlayer(new \Brightcove\Object\Player\Branch\Configuration\Player());
  }
  else {
    $configuration->fieldChanged('player');
  }
  $configuration->getPlayer()->setInactive((bool) $v['inactive']);
  $configuration->setPlaylist((bool) $v['playlist']);
  $configuration->setPlayOnSelect((bool) $v['playOnSelect']);
  $configuration->setPreload($v['preload']);
  $configuration->setScripts(explode(PHP_EOL, $v['scripts']));
  $configuration->setStylesheets(explode(PHP_EOL, $v['stylesheets']));
  $skin = $v['skin'];
  if (is_numeric($skin)) {
    $skin = (bool) $skin;
  }
  $configuration->setSkin($skin);
  if (!$configuration->getStudioConfiguration()) {
    $configuration->setStudioConfiguration(new \Brightcove\Object\Player\Branch\Configuration\StudioConfiguration());
  }
  if (!$configuration->getStudioConfiguration()->getPlayer()) {
    $configuration->getStudioConfiguration()->setPlayer(new \Brightcove\Object\Player\Branch\Configuration\StudioConfigurationPlayer());
  }
  $configuration->getStudioConfiguration()->getPlayer()->setAdjusted((bool) $v['adjusted']);
  $configuration->getStudioConfiguration()->getPlayer()->setHeight($v['height']);
  $configuration->getStudioConfiguration()->getPlayer()->setWidth($v['width']);
}

/**
 * Creates an empty Configuration object.
 *
 * @return \Brightcove\Object\Player\Branch\Configuration\Configuration
 */
function _brightcove_admin_empty_configuration() {
  $configuration = new \Brightcove\Object\Player\Branch\Configuration\Configuration();

  $configuration->setCss(new \Brightcove\Object\Player\Branch\Configuration\CSS());
  $configuration->setPlayer(new \Brightcove\Object\Player\Branch\Configuration\Player());
  $configuration->setLanguages(['en']);
  $configuration->setScripts([
    '//players.brightcove.net/videojs-social/3/videojs-social.min.js',
  ]);
  $configuration->setStylesheets([
    '//players.brightcove.net/videojs-social/3/videojs-social.css',
  ]);

  $studio_configuration = new \Brightcove\Object\Player\Branch\Configuration\StudioConfiguration();
  $studio_configuration->setPlayer(new \Brightcove\Object\Player\Branch\Configuration\StudioConfigurationPlayer());

  $configuration->setStudioConfiguration($studio_configuration);

  return $configuration;
}

