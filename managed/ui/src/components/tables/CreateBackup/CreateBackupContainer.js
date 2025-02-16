// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { CreateBackup } from '../';
import { createTableBackup, createTableBackupResponse } from '../../../actions/tables';
import {
  createUniverseBackup,
  createUniverseBackupResponse,
  fetchUniverseBackups,
  fetchUniverseBackupsResponse
} from '../../../actions/universe';
import { isNonEmptyArray, isNonEmptyObject } from '../../../utils/ObjectUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    createTableBackup: (universeUUID, tableUUID, payload) => {
      Object.keys(payload).forEach((key) => {
        if (typeof payload[key] === 'string' || payload[key] instanceof String)
          payload[key] = payload[key].trim();
      });
      return dispatch(createTableBackup(universeUUID, tableUUID, payload)).then((response) => {
        dispatch(createTableBackupResponse(response.payload));
        if (!response.error) {
          dispatch(fetchUniverseBackups(universeUUID)).then((response) => {
            dispatch(fetchUniverseBackupsResponse(response.payload));
          });
          return response.payload;
        }
        throw new Error(response.error);
      });
    },
    createUniverseBackup: (universeUUID, payload) => {
      Object.keys(payload).forEach((key) => {
        if (typeof payload[key] === 'string' || payload[key] instanceof String)
          payload[key] = payload[key].trim();
      });
      return dispatch(createUniverseBackup(universeUUID, payload)).then((response) => {
        dispatch(createUniverseBackupResponse(response.payload));
        if (!response.error) {
          dispatch(fetchUniverseBackups(universeUUID)).then((response) => {
            dispatch(fetchUniverseBackupsResponse(response.payload));
          });
          return response.payload;
        }
        throw new Error(response.error);
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  const {
    customer: { configs },
    tables: { universeTablesList }
  } = state;
  const storageConfigs = configs.data.filter((config) => config.type === 'STORAGE');
  const initialFormValues = {
    enableSSE: false,
    transactionalBackup: false,
    parallelism: 8,
    schedulingFrequencyUnit: {
      value: 'Hours',
      label: 'Hours'
    }
  };

  if (isNonEmptyObject(ownProps.tableInfo)) {
    initialFormValues.backupTableUUID = ownProps.tableInfo.tableID;
  } else if (isNonEmptyArray(universeTablesList.data)) {
    initialFormValues.backupTableUUID = universeTablesList.tableUUID;
  }

  if (isNonEmptyArray(storageConfigs)) {
    initialFormValues.storageConfigUUID = {
      value: storageConfigs[0].configUUID,
      label: storageConfigs[0].name + ' Storage',
      id: storageConfigs[0].name.toUpperCase()
    };
  }

  const tablesList = state.tables.universeTablesList.filter((table) => !table.isIndexTable);
  return {
    storageConfigs: storageConfigs,
    universeDetails: state.universe.currentUniverse.data.universeDetails,
    universeTables: tablesList,
    initialValues: initialFormValues
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(CreateBackup);
